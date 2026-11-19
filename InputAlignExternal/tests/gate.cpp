// SPDX-License-Identifier: MIT
#include "Protocol.hpp"
#include "PhysicsClock.hpp"
#include "AtomicFile.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#define CHECK(expression) do { if (!(expression)) { std::cerr << "Failed: " #expression " at " << __LINE__ << '\n'; return 1; } } while (false)
using namespace iax;
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--frame") {
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        Frame frame;
        frame.header = {Magic, FrameKind, 0x11, 0x22, 7, 10, 0};
        frame.state = {12.5, 105, -1.25, .9, Valid | Ground};
        frame.history = 0b101; frame.historyCount = 10; frame.horizon = 400;
        std::cout.write(reinterpret_cast<char*>(&frame), sizeof(frame));
        return 0;
    }
    PhysicsClock clock;
    CHECK(!clock.ready() && clock.tick == 0);
    clock.advance(0.f, true); // Command/zero-duration work contributes no replay step.
    CHECK(!clock.ready() && clock.tick == 0 && clock.history == 0);
    clock.advance(.25f, true);
    CHECK(clock.ready() && clock.tick == 1 && clock.history == 1);
    clock.advance(.25f, false);
    CHECK(clock.tick == 2 && clock.history == 2);
    clock.advance(.125f, true); // Partial physics suspends correction until a reset.
    CHECK(!clock.ready() && clock.tick == 2 && clock.history == 2);
    clock.advance(.25f, true);
    CHECK(!clock.ready() && clock.tick == 2);
    clock.reset(); clock.advance(.25f, false);
    CHECK(clock.ready() && clock.tick == 1 && clock.history == 0);
    clock.advance(std::numeric_limits<float>::quiet_NaN(), false);
    CHECK(!clock.ready());
    clock.reset();
    for (int i = 0; i < 70; ++i) clock.advance(.25f, i == 69);
    CHECK(clock.tick == 70 && clock.history == 1);
    auto temporary = std::filesystem::temp_directory_path() /
        ("iax-save-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(temporary);
    auto recording = temporary / "attempt.json";
    CHECK(!saveAtomically(recording, [](std::ostream& out) {
        out << "incomplete";
        out.setstate(std::ios::badbit);
    }));
    CHECK(!std::filesystem::exists(recording));
    CHECK(std::filesystem::exists(temporary / "attempt.json.partial"));
    CHECK(saveAtomically(recording, [](std::ostream& out) { out << "{\"inputs\":[]}\n"; }));
    CHECK(std::filesystem::exists(recording));
    CHECK(!std::filesystem::exists(temporary / "attempt.json.partial"));
    CHECK(!saveAtomically(temporary / "missing" / "attempt.json", [](std::ostream& out) { out << "{}"; }));
    std::filesystem::remove_all(temporary);
    Gate gate; gate.reset(7);
    State actual{12.5, 105, -1.25, .9, Ground};
    Decision d;
    d.header = {Magic, DecisionKind, 0x11, 0x22, 7, 10, 0};
    d.expected = actual; d.due = {25, 105, 0, .9, Ground}; d.delay = 3; d.down = 1;
    if (argc == 3 && std::string(argv[1]) == "--decision") {
        std::ifstream in(argv[2], std::ios::binary); in.read(reinterpret_cast<char*>(&d), sizeof(d));
        CHECK(in.gcount() == sizeof(d));
    }
    CHECK(gate.accept(d, 9));
    CHECK(!gate.request(9, actual, true));
    CHECK(!gate.request(10, actual, false));
    CHECK(!gate.request(10, actual, true, 2));
    CHECK(gate.request(10, actual, true));
    CHECK(!gate.poll(12, d.due, true));
    auto delivered = gate.poll(13, d.due, true);
    CHECK(delivered && delivered->down && delivered->corrected);
    CHECK(!gate.poll(13, d.due, true));
    CHECK(!gate.accept(d, 11));
    auto invalid = d; invalid.header.tick = 999; CHECK(!gate.accept(invalid, 1));
    invalid = d; ++invalid.header.session; CHECK(!gate.accept(invalid, 10));
    invalid = d; invalid.down = 2; CHECK(!gate.accept(invalid, 10));
    invalid = d; invalid.delay = -1; CHECK(!gate.accept(invalid, 10));
    invalid = d; invalid.delay = 401; CHECK(!gate.accept(invalid, 10));
    invalid = d; invalid.expected.x = std::numeric_limits<double>::quiet_NaN(); CHECK(!gate.accept(invalid, 10));
    invalid = d; invalid.pointCount = 33; CHECK(!gate.accept(invalid, 10));
    invalid = d; invalid.pointCount = 1; invalid.points[1] = std::numeric_limits<float>::infinity();
    CHECK(!gate.accept(invalid, 10));
    CHECK(gate.accept(d, 10));
    State wrong = actual; wrong.y += 1;
    CHECK(!gate.request(10, wrong, true));
    CHECK(gate.request(10, actual, true));
    auto fallback = gate.poll(11, wrong, false);
    CHECK(fallback && fallback->down && !fallback->corrected);
    CHECK(gate.accept(d, 10)); CHECK(gate.request(10, actual, true));
    fallback = gate.poll(11, actual, true, 0);
    CHECK(fallback && fallback->down && !fallback->corrected);
    CHECK(gate.accept(d, 10)); CHECK(gate.request(10, actual, true));
    fallback = gate.poll(13, wrong, true);
    CHECK(fallback && fallback->down && !fallback->corrected);
    CHECK(gate.accept(d, 10)); CHECK(gate.request(10, actual, true));
    gate.changed(); CHECK(!gate.pending()); CHECK(!gate.accept(d, 10));
    gate.reset(7); CHECK(gate.accept(d, 10)); CHECK(gate.request(10, actual, true));
    CHECK(!gate.request(11, actual, false)); CHECK(!gate.pending());
    gate.reset(8); CHECK(!gate.accept(d, 10));
    return 0;
}
