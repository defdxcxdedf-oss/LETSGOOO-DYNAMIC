// Copyright (c) 2026 dadjdjxjfjdjd-sketch. SPDX-License-Identifier: MIT
// Compile the documented simulator fork; the task-owned adapter keeps the search loop out of Python.
#include "vendor/gdgen/sim/core/gdsim.cpp"
#include "Protocol.hpp"
#include "CandidatePool.hpp"

namespace {
iax::State wire(gd::Player const& p) {
    std::uint32_t flags = p.mode;
    if (p.mini) flags |= iax::Mini;
    if (p.upside_down) flags |= iax::UpsideDown;
    if (p.on_ground) flags |= iax::Ground;
    if (p.held) flags |= iax::Held;
    return {p.x, p.y, p.y_vel, p.speed, flags};
}
}

// Return 1 only for a fully checked correction; 0=direct, 2=deadline, 3=unsupported, -1=error.
extern "C" void iax_clear_preview_notes(void* level) noexcept {
    if (level) static_cast<gd::Level*>(level)->unsupported.clear();
}

extern "C" int iax_adapter_version() noexcept { return 4; }

extern "C" int iax_predict_v2(void* level, void* state, int horizon, int budgetUs, int lead, int collect, int currentHeld,
                           iax::Decision* output) noexcept {
    if (!level || !state || !output || horizon < 1 || horizon > 400 || budgetUs < 500 || budgetUs > 20000
        || lead < 1 || lead > 16 || collect < 0 || collect > 1 || currentHeld < 0 || currentHeld > 1) return -1;
    try {
        using Timer = std::chrono::steady_clock;
        auto deadline = Timer::now() + std::chrono::microseconds(budgetUs);
        auto& layout = *static_cast<gd::Level*>(level);
        struct DeadlineScope {
            gd::Level& layout;
            bool active;
            Timer::time_point previous;
            ~DeadlineScope() { layout.deadlineActive = active; layout.deadline = previous; }
        } deadlineScope{layout, layout.deadlineActive, layout.deadline};
        layout.deadlineActive = true;
        layout.deadline = deadline;
        // Notes from hypothetical futures must not poison the real reconstructed state.
        struct NotesScope {
            gd::Level& layout;
            std::map<std::string, double> saved;
            ~NotesScope() { layout.unsupported.swap(saved); }
        } notes{layout, layout.unsupported};
        gd::Simulator simulator(layout);
        auto base = *static_cast<gd::Player*>(state);
        bool held = currentHeld != 0;
        for (int i = 0; i < lead; ++i) base = simulator.step(base, held);
        if (!layout.unsupported.empty()) return 3;
        if (base.dead) return 0;
        std::array<float, 64> points{};
        std::uint32_t pointCount = 0;
        auto survives = [&](gd::Player branch, bool action, bool capture) {
            pointCount = 0;
            for (int i = 0; i < horizon; ++i) {
                if (Timer::now() >= deadline) return -1;
                branch = simulator.step(branch, action);
                if (!layout.unsupported.empty()) return -2;
                if (branch.dead) return 0;
                if (capture && i % ((horizon + 31) / 32) == 0 && pointCount < 32) {
                    points[pointCount * 2] = static_cast<float>(branch.x);
                    points[pointCount * 2 + 1] = static_cast<float>(branch.y);
                    ++pointCount;
                }
            }
            return 1;
        };
        int immediate = survives(base, !held, false);
        if (immediate != 0) return immediate == 1 ? 0 : immediate == -1 ? 2 : 3;
        auto delayed = base;
        bool prefixEnded = false;
        for (int first = 1; first <= horizon && !prefixEnded; first += 7) {
            std::array<gd::Player,7> futures{}, due{};
            std::array<std::array<float,64>,7> traces{};
            std::array<unsigned,7> counts{};
            int count = 0;
            for (int i = 0; i < 7 && first+i <= horizon; ++i) {
                layout.check_deadline();
                delayed = simulator.step(delayed, held);
                if (!layout.unsupported.empty() || delayed.dead) {
                    layout.unsupported.clear();
                    prefixEnded = true;
                    break;
                }
                due[count] = futures[count] = delayed;
                ++count;
            }
            int winner = iax::evaluateCandidates(futures,count,horizon,
                [&](gd::Player& branch,int index,int tick) {
                    layout.check_deadline();
                    branch = simulator.step(branch,!held);
                    if (!layout.unsupported.empty()) {
                        layout.unsupported.clear();
                        return -1;
                    }
                    if (branch.dead) return 0;
                    auto& n = counts[index];
                    if (collect && tick % ((horizon+31)/32) == 0 && n < 32) {
                        traces[index][n*2] = static_cast<float>(branch.x);
                        traces[index][n*2+1] = static_cast<float>(branch.y);
                        ++n;
                    }
                    return 1;
                });
            if (winner >= 0) {
                iax::Decision result;
                result.expected = wire(base); result.due = wire(due[winner]);
                result.delay = first+winner; result.down = unsigned(!held);
                result.pointCount = counts[winner]; result.points = traces[winner];
                layout.check_deadline();
                *output = result;
                return 1;
            }
        }
        return 0;
    } catch (gd::DeadlineExceeded const&) { return 2; }
    catch (...) { return -1; }
}
