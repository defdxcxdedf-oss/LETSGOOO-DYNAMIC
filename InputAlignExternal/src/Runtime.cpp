// Copyright (c) 2026 dadjdjxjfjdjd-sketch. SPDX-License-Identifier: MIT
#include "Runtime.hpp"
#include "AtomicFile.hpp"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <random>
#include <sstream>
#include <thread>

using namespace geode::prelude;
namespace iax {
std::shared_ptr<Runtime> active;
namespace {
template<class T> T setting(char const* key) { return Mod::get()->getSettingValue<T>(key); }
int integer(char const* key) { return static_cast<int>(setting<int64_t>(key)); }
}
void Settings::reload() {
    enabled = setting<bool>("enabled"); practiceOnly = setting<bool>("buffer-practice-only");
    normal = setting<bool>("buffer-normal"); wave = setting<bool>("buffer-wave");
    cbfSync = setting<bool>("cbf-sync"); spam = setting<bool>("spam-guard");
    invisible = setting<bool>("invisible-trajectory"); performance = setting<bool>("performance-mode");
    record = setting<bool>("macro-record"); compare = setting<bool>("macro-compare");
    noclip = setting<bool>("noclip"); noTrail = setting<bool>("no-wave-trail");
    normalTicks = std::clamp(integer("normal-ticks"), 0, 400);
    waveTicks = std::clamp(integer("wave-ticks"), 0, 400);
    if (setting<bool>("all-ticks-400")) normalTicks = waveTicks = 400;
    interval = std::clamp(integer("spam-interval"), 4, 400);
    budget = std::clamp(integer("max-correction-ms"), 1, 20);
    speed = static_cast<float>(std::clamp(setting<double>("game-speed"), .1, 2.));
}
Runtime::Runtime(PlayLayer* layer) : owner(layer) {
    settings.reload();
    m_writer = std::thread([this] { writeRecordings(); });
}
Runtime::~Runtime() {
    flushRecording();
    { std::lock_guard lock(m_writeMutex); m_stopping = true; }
    m_writeReady.notify_one();
    if (m_writer.joinable()) m_writer.join();
    if (m_socket != INVALID_SOCKET) closesocket(m_socket);
    if (m_wsa) WSACleanup();
    if (m_process) CloseHandle(m_process);
}
std::filesystem::path Runtime::directory() const { return Mod::get()->getSaveDir(); }
bool Runtime::start(std::string const& level) {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data)) { status = "Error al iniciar Winsock"; return false; }
    m_wsa = true;
    m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket == INVALID_SOCKET) { status = "No se pudo crear el puente"; return false; }
    sockaddr_in address{};
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(m_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address))) return false;
    u_long nonblocking = 1;
    if (ioctlsocket(m_socket, FIONBIO, &nonblocking)) {
        closesocket(m_socket); m_socket = INVALID_SOCKET;
        status = "No se pudo configurar el puente sin bloqueo";
        return false;
    }
    int length = sizeof(address);
    if (getsockname(m_socket, reinterpret_cast<sockaddr*>(&address), &length)) return false;
    std::random_device random;
    m_hi = (std::uint64_t(random()) << 32) | random();
    m_lo = (std::uint64_t(random()) << 32) | random();
    try {
        std::filesystem::create_directories(directory() / "worker");
        std::ofstream output(directory() / "level.txt", std::ios::binary | std::ios::trunc);
        output.write(level.data(), static_cast<std::streamsize>(level.size()));
        output.close();
        if (!output) throw std::runtime_error("level.txt");
        matjson::Value config = matjson::makeObject({
            {"version", 1}, {"port", ntohs(address.sin_port)},
            {"tokenHi", fmt::format("{:016x}", m_hi)}, {"tokenLo", fmt::format("{:016x}", m_lo)}
        });
        std::ofstream bridge(directory() / "bridge.json", std::ios::binary | std::ios::trunc);
        bridge << config.dump();
        bridge.close();
        if (!bridge) throw std::runtime_error("bridge.json");
    } catch (std::exception const& error) {
        status = "No se pudieron exportar los datos";
        log::warn("Input Align External: {}", error.what());
        return false;
    }
    m_ready = true;
    refresh(); reset();
    return true;
}
void Runtime::refresh() {
    settings.reload();
    if (m_recordingError.exchange(false))
        status = "Error al guardar macro; se reintentara en el siguiente intento";
    cbfPresent = Loader::get()->getLoadedMod("syzzi.click_between_frames") != nullptr;
    conflict = Loader::get()->getLoadedMod("ai.predictivebuffer") != nullptr;
    auto path = setting<std::filesystem::path>("macro-file");
    if (path != m_macroPath) { m_macroPath = path; loadMacro(); }
}
void Runtime::reset() {
    flushRecording();
    if (!m_macroLoaded) loadMacro();
    ++session; m_physics.reset(); held = false; m_pendingEvent.reset();
    gate.reset(session); applying = false; inCommands = false; fullTick = false; m_predictionReady = false;
    clicks = corrected = noclipDeaths = 0; clickTimes.clear(); m_recentPresses.clear();
}
bool Runtime::connected() const {
    return m_connected && Clock::now() - m_lastPacket < std::chrono::seconds(2);
}
bool Runtime::eligible() const {
    auto player = owner->m_player1;
    return settings.enabled && settings.invisible && connected() && m_predictionReady && !conflict
        && player && !player->m_isDead && !owner->m_isPlatformer
        && !owner->m_gameState.m_isDualMode
        && (!settings.practiceOnly || owner->m_isPracticeMode)
        && !(owner->m_isPracticeMode && settings.noclip)
        && !cbfPresent && fullTick && m_physics.ready()
        && (player->m_isDart ? settings.wave : settings.normal);
}
bool Runtime::observedHeld() const {
    auto player = owner->m_player1;
    if (!player) return false;
    auto it = player->m_holdingButtons.find(1);
    return it != player->m_holdingButtons.end() && it->second;
}
State Runtime::state() const {
    State out;
    if (auto p = owner->m_player1) {
        out.x = p->getPositionX(); out.y = p->getPositionY(); out.vy = p->m_yVelocity; out.speed = p->m_playerSpeed;
        out.flags = p->m_isShip ? 1 : p->m_isBall ? 2 : p->m_isBird ? 3 : p->m_isDart ? 4
                  : p->m_isRobot ? 5 : p->m_isSpider ? 6 : p->m_isSwing ? 7 : 0;
        if (p->m_vehicleSize < .8f) out.flags |= Mini;
        if (p->m_isUpsideDown) out.flags |= UpsideDown;
        if (p->m_isOnGround) out.flags |= Ground;
        if (observedHeld()) out.flags |= Held;
    }
    return out;
}
void Runtime::receive() {
    auto const tick = m_physics.tick;
    if (!m_ready || m_socket == INVALID_SOCKET) return;
    std::array<char, 1024> buffer{};
    for (int i = 0; i < 8; ++i) { // Bound network work even if another process floods the port.
        sockaddr_in source{}; int length = sizeof(source);
        int size = recvfrom(m_socket, buffer.data(), int(buffer.size()), 0, reinterpret_cast<sockaddr*>(&source), &length);
        if (size < int(sizeof(Header))) break;
        Header header; std::memcpy(&header, buffer.data(), sizeof(header));
        if (source.sin_addr.s_addr != htonl(INADDR_LOOPBACK) || header.magic != Magic
            || header.tokenHi != m_hi || header.tokenLo != m_lo) continue;
        if (header.kind == Hello && size == sizeof(Header)) {
            m_worker = source; m_connected = true; m_lastPacket = Clock::now();
            continue;
        }
        if (!m_connected || source.sin_port != m_worker.sin_port || header.session != session) continue;
        if (header.kind == DecisionKind && size == sizeof(Decision)) {
            Decision result; std::memcpy(&result, buffer.data(), sizeof(result));
            if (gate.accept(result, tick)) { m_lastPacket = Clock::now(); m_predictionReady = true; }
        } else if (header.kind == StatusKind && size == sizeof(Status)) {
            Status result; std::memcpy(&result, buffer.data(), sizeof(result)); result.text[119] = '\0';
            status = result.text; m_lastPacket = Clock::now(); m_predictionReady = result.code == 0;
        }
    }
}
void Runtime::before(float dt, bool half) {
    auto const tick = m_physics.tick;
    inCommands = true;
    // processCommands' delta convention is not a physics quantum. Observe PlayerObject::update instead.
    fullTick = !half && m_physics.ready() && std::isfinite(dt);
    receive();
    int limit = owner->m_player1 && owner->m_player1->m_isDart ? settings.waveTicks : settings.normalTicks;
    if (auto result = gate.poll(tick, state(), eligible(), limit)) {
        applying = true;
        owner->handleButton(result->down, 1, true);
        applying = false;
        if (result->corrected) ++corrected;
        if (m_pendingEvent && *m_pendingEvent < m_events.size()) {
            auto& event = m_events[*m_pendingEvent];
            event.applied = true; event.corrected = result->corrected; event.appliedTick = tick;
        }
        m_pendingEvent.reset();
    }
    if (!connected()) return;
    Frame frame;
    frame.header = {Magic, FrameKind, m_hi, m_lo, session, tick, gate.epoch()};
    frame.state = state(); if (eligible()) frame.state.flags |= Valid;
    frame.history = m_physics.history; frame.historyCount = static_cast<std::uint32_t>(std::min<std::uint64_t>(tick, 64));
    frame.horizon = owner->m_player1 && owner->m_player1->m_isDart ? settings.waveTicks : settings.normalTicks;
    frame.budgetUs = settings.budget * (settings.performance ? 500 : 1000);
    frame.options = 0; // Original invisible-trajectory switch gates correction; no trajectory is drawn.
    sendto(m_socket, reinterpret_cast<char const*>(&frame), sizeof(frame), 0,
           reinterpret_cast<sockaddr*>(&m_worker), sizeof(m_worker)); // Nonblocking; loss is handled by history.
}
void Runtime::after() {
    applied(observedHeld());
    inCommands = false;
}
void Runtime::physicsStep(float dt, bool heldDuringStep) {
    m_physics.advance(dt, heldDuringStep);
    if (!m_physics.aligned) {
        fullTick = false;
        m_predictionReady = false;
        status = "Paso fisico no compatible; reinicia el intento";
    }
}
bool Runtime::input(bool down, int button, bool player1) {
    auto const tick = m_physics.tick;
    if (applying || button != 1 || !player1) return false;
    if (held == down && !gate.pending()) return false;
    if (down) {
        ++clicks; clickTimes.push_back(Clock::now());
        m_recentPresses.push_back(tick);
        if (m_recentPresses.size() > 3) m_recentPresses.pop_front();
    }
    Event event{tick, tick, down};
    if (settings.compare && settings.record) {
        // Two nearest neighbours in a per-action index: bounded even for a dense 100000-input macro.
        auto const& ticks = m_referenceTicks[unsigned(down)];
        auto candidate = std::lower_bound(ticks.begin(), ticks.end(), tick);
        auto consider = [&event, tick](std::uint64_t reference) {
            if (reference <= tick + 80 && (tick <= 80 || reference >= tick - 80)) {
                int delta = static_cast<int>(static_cast<int64_t>(tick) - static_cast<int64_t>(reference));
                if (std::abs(delta) < std::abs(event.referenceDelta)) event.referenceDelta = delta;
            }
        };
        if (candidate != ticks.begin()) consider(*std::prev(candidate));
        if (candidate != ticks.end()) consider(*candidate);
    }
    bool spam = m_recentPresses.size() == 3
        && m_recentPresses[1] - m_recentPresses[0] < unsigned(settings.interval)
        && m_recentPresses[2] - m_recentPresses[1] < unsigned(settings.interval)
        && tick - m_recentPresses.back() < unsigned(settings.interval);
    bool queue = inCommands && eligible() && !(settings.spam && spam);
    int limit = owner->m_player1 && owner->m_player1->m_isDart ? settings.waveTicks : settings.normalTicks;
    auto prediction = queue ? gate.request(tick, state(), down, limit) : std::nullopt;
    if (!prediction) gate.clear();
    event.applied = !prediction;
    m_pendingEvent.reset();
    if (settings.record && m_events.size() < 100000) {
        m_events.push_back(event);
        if (prediction) m_pendingEvent = m_events.size() - 1;
    }
    if (prediction) return true;
    return false;
}
void Runtime::applied(bool down) { if (held != down) { held = down; gate.changed(); } }

void Runtime::loadMacro() {
    m_reference.clear();
    for (auto& ticks : m_referenceTicks) ticks.clear();
    m_macroLoaded = m_macroPath.empty();
    if (m_macroLoaded) return;
    try {
        if (std::filesystem::file_size(m_macroPath) > 16 * 1024 * 1024) throw std::runtime_error("macro >16 MiB");
        std::ifstream file(m_macroPath, std::ios::binary);
        if (!file) throw std::runtime_error("macro unavailable");
        std::string text((std::istreambuf_iterator<char>(file)), {});
        auto add = [this](int64_t tick, bool down) {
            if (tick >= 0 && tick <= 1000000000 && m_reference.size() < 100000) m_reference.push_back({std::uint64_t(tick), down});
        };
        if (m_macroPath.extension() == ".json") {
            auto value = matjson::parse(text);
            if (!value || !value.unwrap()["inputs"].isArray()) throw std::runtime_error("inputs ausente");
            auto const& json = value.unwrap();
            double fps = json["framerate"].asDouble().unwrapOr(240.);
            if (!std::isfinite(fps) || fps <= 0 || fps > 100000) throw std::runtime_error("framerate invalido");
            for (auto const& input : json["inputs"].asArray().unwrap()) {
                if (input["2p"].asBool().unwrapOr(false) || input["player2"].asBool().unwrapOr(false)
                    || input["button"].asInt().unwrapOr(1) != 1) continue;
                auto frame = input["frame"].asInt(); auto down = input["down"].asBool();
                if (frame && down && frame.unwrap() >= 0 && frame.unwrap() <= 1000000000)
                    add(static_cast<int64_t>(std::llround(frame.unwrap() * 240. / fps)), down.unwrap());
            }
        } else if (m_macroPath.extension() == ".txt" || m_macroPath.extension() == ".csv") {
            std::replace(text.begin(), text.end(), ',', ' ');
            std::istringstream lines(text); std::string line;
            while (std::getline(lines, line)) {
                std::istringstream row(line); int64_t frame; std::string action;
                if (row >> frame >> action) {
                    if (action == "1" || action == "press" || action == "down") add(frame, true);
                    else if (action == "0" || action == "release" || action == "up") add(frame, false);
                }
            }
        } else throw std::runtime_error("formato no compatible");
        std::stable_sort(m_reference.begin(), m_reference.end(), [](auto const& a, auto const& b) { return a.tick < b.tick; });
        for (auto const& input : m_reference) m_referenceTicks[unsigned(input.down)].push_back(input.tick);
        m_macroLoaded = true;
    } catch (std::exception const& error) {
        m_reference.clear(); for (auto& ticks : m_referenceTicks) ticks.clear();
        log::warn("Input Align External macro: {}", error.what());
    }
}
void Runtime::flushRecording() {
    {
        std::lock_guard lock(m_writeMutex);
        // Retry retained failures on the next attempt/save, without spinning on a full disk.
        while (!m_failedRecordings.empty() && m_recordings.size() < (m_events.empty() ? 16u : 15u)) {
            m_recordings.push_back(std::move(m_failedRecordings.front()));
            m_failedRecordings.pop_front();
        }
        if (!m_events.empty()) {
            auto path = directory() / fmt::format("attempt-{}-{}.json", Clock::now().time_since_epoch().count(), ++m_recordSerial);
            if (m_recordings.size() < 16) m_recordings.push_back({path, std::move(m_events), session});
            else { m_recordingError = true; log::warn("Input Align External: recording queue full"); }
        }
    }
    m_events.clear(); m_pendingEvent.reset();
    m_writeReady.notify_one();
}
void Runtime::writeRecordings() {
    // Disk work owns copied data and never touches game objects. Joined on level exit.
    for (;;) {
        Recording job;
        {
            std::unique_lock lock(m_writeMutex);
            m_writeReady.wait(lock, [this] { return m_stopping || !m_recordings.empty(); });
            if (m_recordings.empty()) return;
            job = std::move(m_recordings.front()); m_recordings.pop_front();
        }
        bool saved = saveAtomically(job.path, [&job](std::ostream& out) {
            out << "{\"schema\":1,\"framerate\":240,\"session\":" << job.attempt << ",\"inputs\":[";
            bool first = true;
            for (auto const& e : job.events) {
                if (!first) out << ',';
                first = false;
                out << "{\"frame\":" << e.tick << ",\"down\":" << (e.down ? "true" : "false")
                    << ",\"applied\":" << (e.applied ? "true" : "false") << ",\"appliedFrame\":" << e.appliedTick
                    << ",\"corrected\":" << (e.corrected ? "true" : "false");
                if (e.referenceDelta != 1000000) out << ",\"referenceDelta\":" << e.referenceDelta;
                out << '}';
            }
            out << "]}\n";
        });
        if (!saved) {
            m_recordingError = true;
            log::warn("Input Align External: recording write failed; retaining up to 16 attempts until level exit");
            std::lock_guard lock(m_writeMutex);
            if (m_failedRecordings.size() < 16) m_failedRecordings.push_back(std::move(job));
        }
    }
}
void Runtime::launchWorker() {
    if (!m_ready) { Notification::create("Puente no disponible", NotificationIcon::Error)->show(); return; }
    if (m_process) {
        DWORD code = 0;
        if (GetExitCodeProcess(m_process, &code) && code == STILL_ACTIVE) {
            Notification::create("La consola ya esta abierta", NotificationIcon::Info)->show(); return;
        }
        CloseHandle(m_process); m_process = nullptr;
    }
    auto exe = directory() / "worker" / "python.exe";
    if (!std::filesystem::is_regular_file(exe)
        || !std::filesystem::is_regular_file(directory() / "worker" / "main.py")
        || !std::filesystem::is_regular_file(directory() / "worker" / "gdsim.dll")) {
        geode::utils::file::openFolder(directory() / "worker");
        FLAlertLayer::create("Input Align External", "Copia aqui el contenido de <cg>InputAlignWorker</c> del paquete compilado. Despues pulsa <cy>Abrir CMD</c> y reinicia el intento.", "OK")->show();
        return;
    }
    // Explicit application name and quoted arguments: no cmd.exe or shell string interpolation.
    auto command = L"\"" + exe.wstring() + L"\" -X utf8 \"" + (directory() / "worker" / "main.py").wstring() + L"\"";
    command += L" --bridge \"" + directory().wstring() + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
    if (CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE,
                       CREATE_NEW_CONSOLE | BELOW_NORMAL_PRIORITY_CLASS, nullptr, exe.parent_path().c_str(), &startup, &process)) {
        m_process = process.hProcess; CloseHandle(process.hThread);
        status = "Consola iniciada; reinicia el intento";
    } else Notification::create("No se pudo abrir la consola", NotificationIcon::Error)->show();
}
}
