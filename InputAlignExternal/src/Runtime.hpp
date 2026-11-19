// Copyright (c) 2026 dadjdjxjfjdjd-sketch. SPDX-License-Identifier: MIT
#pragma once
// Winsock must precede Windows headers (also included by Geode).
#include <winsock2.h>
#include <windows.h>
#include <Geode/Geode.hpp>
#include "Protocol.hpp"
#include "PhysicsClock.hpp"
#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <string>
#include <vector>

namespace iax {
using Clock = std::chrono::steady_clock;
struct Settings {
    bool enabled = true, practiceOnly = false, normal = true, wave = true, cbfSync = true;
    bool spam = false, invisible = true, performance = false, record = true, compare = true;
    bool noclip = false, noTrail = false;
    int normalTicks = 400, waveTicks = 400, interval = 20, budget = 6;
    float speed = 1.f;
    void reload();
};
struct MacroInput { std::uint64_t tick; bool down; };
struct Event {
    std::uint64_t tick, appliedTick;
    bool down, applied = false, corrected = false;
    int referenceDelta = 1000000;
};

class Runtime {
    SOCKET m_socket = INVALID_SOCKET;
    bool m_wsa = false, m_connected = false, m_predictionReady = false;
    sockaddr_in m_worker{};
    std::uint64_t m_hi = 0, m_lo = 0;
    HANDLE m_process = nullptr;
    Clock::time_point m_lastPacket{};
    std::vector<MacroInput> m_reference;
    std::array<std::vector<std::uint64_t>, 2> m_referenceTicks;
    std::filesystem::path m_macroPath;
    bool m_macroLoaded = false;
    std::vector<Event> m_events;
    std::optional<std::size_t> m_pendingEvent;
    std::uint64_t m_recordSerial = 0;
    struct Recording { std::filesystem::path path; std::vector<Event> events; std::uint64_t attempt; };
    std::mutex m_writeMutex;
    std::condition_variable m_writeReady;
    std::deque<Recording> m_recordings, m_failedRecordings;
    std::atomic<bool> m_recordingError = false;
    PhysicsClock m_physics;
    bool m_stopping = false, m_ready = false;
    std::thread m_writer;
    std::deque<std::uint64_t> m_recentPresses;
    void writeRecordings();
    void receive();
    void loadMacro();
public:
    PlayLayer* owner;
    Settings settings;
    Gate gate;
    std::uint64_t session = 0, clicks = 0, corrected = 0, noclipDeaths = 0;
    bool held = false, applying = false, inCommands = false, fullTick = false;
    bool cbfPresent = false;
    bool conflict = false;
    std::string status = "Consola desconectada";
    std::deque<Clock::time_point> clickTimes;
    explicit Runtime(PlayLayer* layer);
    ~Runtime();
    bool start(std::string const& level);
    void reset();
    void refresh();
    bool eligible() const;
    bool observedHeld() const;
    State state() const;
    void before(float dt, bool half);
    void after();
    void physicsStep(float dt, bool heldDuringStep);
    bool input(bool down, int button, bool player1); // true means queued; otherwise call original.
    void applied(bool down);
    void flushRecording();
    void launchWorker();
    std::filesystem::path directory() const;
    bool connected() const;
};
extern std::shared_ptr<Runtime> active;
void showPanel();
void addPauseButton(PauseLayer* pause);
}
