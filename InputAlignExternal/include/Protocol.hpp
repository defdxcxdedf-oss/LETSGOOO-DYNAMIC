// Copyright (c) 2026 dadjdjxjfjdjd-sketch. SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <type_traits>

namespace iax {
constexpr std::uint32_t Magic = 0x31584149; // IAX1, little endian; protocol version 1
enum Kind : std::uint32_t { Hello = 1, FrameKind = 2, DecisionKind = 3, StatusKind = 4 };
enum Flags : std::uint32_t { Mini = 16, UpsideDown = 32, Ground = 64, Held = 128, Valid = 256 };
#pragma pack(push, 1)
struct Header {
    std::uint32_t magic = Magic, kind = 0;
    std::uint64_t tokenHi = 0, tokenLo = 0, session = 0, tick = 0, epoch = 0;
};
struct State {
    double x = 0, y = 0, vy = 0, speed = 0;
    std::uint32_t flags = 0;
};
struct Frame {
    Header header;
    State state;
    std::uint64_t history = 0;
    std::uint32_t historyCount = 0, horizon = 0, budgetUs = 6000, options = 0;
};
struct Decision {
    Header header;
    State expected, due;
    std::int32_t delay = 0;
    std::uint32_t down = 0, pointCount = 0;
    std::array<float, 64> points{};
};
struct Status {
    Header header;
    std::uint32_t code = 0;
    char text[120]{};
};
#pragma pack(pop)
static_assert(sizeof(Header) == 48 && sizeof(State) == 36);
static_assert(sizeof(Frame) == 108 && sizeof(Decision) == 388 && sizeof(Status) == 172);
static_assert(std::is_trivially_copyable_v<Frame>);

inline bool finite(State const& s) {
    return std::isfinite(s.x) && std::isfinite(s.y) && std::isfinite(s.vy) && std::isfinite(s.speed);
}
inline bool matches(State const& a, State const& b) {
    constexpr auto mask = 0xffu; // Valid is transport eligibility, not physical state.
    return finite(a) && finite(b) && (a.flags & mask) == (b.flags & mask)
        && std::abs(a.x - b.x) <= .10 && std::abs(a.y - b.y) <= .10
        && std::abs(a.vy - b.vy) <= .05 && std::abs(a.speed - b.speed) <= .0001;
}

// Bounded main-thread decision cache. A delayed input is revalidated at its due state.
class Gate {
    struct Entry { bool used = false; Decision decision{}; };
    std::array<Entry, 32> m_cache{};
    std::optional<Decision> m_pending;
    std::uint64_t m_session = 0, m_epoch = 0;
public:
    struct Result { bool down, corrected; };
    void reset(std::uint64_t session) { m_session = session; m_epoch = 0; clear(); }
    void clear() { for (auto& e : m_cache) e.used = false; m_pending.reset(); }
    void changed() { ++m_epoch; clear(); }
    std::uint64_t epoch() const { return m_epoch; }
    bool pending() const { return m_pending.has_value(); }
    bool accept(Decision const& d, std::uint64_t tick) {
        if (d.header.magic != Magic || d.header.kind != DecisionKind || d.header.session != m_session
            || d.header.epoch != m_epoch || d.header.tick < tick || d.header.tick - tick > 16
            || d.delay < 1 || d.delay > 400 || d.down > 1 || d.pointCount > 32
            || !finite(d.expected) || !finite(d.due)) return false;
        for (std::uint32_t i = 0; i < d.pointCount * 2; ++i)
            if (!std::isfinite(d.points[i])) return false;
        m_cache[d.header.tick % m_cache.size()] = {true, d};
        return true;
    }
    std::optional<Decision> request(std::uint64_t tick, State const& state, bool down, int limit = 400) {
        // A physical edge supersedes any queued edge, including a delayed press followed by a release.
        m_pending.reset();
        auto& e = m_cache[tick % m_cache.size()];
        if (!e.used || e.decision.header.tick != tick || e.decision.header.epoch != m_epoch
            || e.decision.down != unsigned(down) || e.decision.delay > limit || !matches(state, e.decision.expected)) return {};
        m_pending = e.decision;
        e.used = false;
        return m_pending;
    }
    std::optional<Result> poll(std::uint64_t tick, State const& state, bool eligible, int limit = 400) {
        if (!m_pending) return {};
        auto dueTick = m_pending->header.tick + unsigned(m_pending->delay);
        if (!eligible || tick > dueTick || m_pending->delay > limit) {
            auto down = bool(m_pending->down); m_pending.reset();
            return Result{down, false}; // Flush the physical edge when prediction becomes unavailable.
        }
        if (tick < dueTick) return {};
        auto d = *m_pending;
        m_pending.reset();
        return Result{bool(d.down), matches(state, d.due)};
    }
};
}
