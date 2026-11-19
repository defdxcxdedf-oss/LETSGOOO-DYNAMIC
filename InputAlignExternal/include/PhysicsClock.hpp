// Copyright (c) 2026 dadjdjxjfjdjd-sketch. SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstdint>

namespace iax {
// Counts completed player physics steps, never command-processing calls.
struct PhysicsClock {
    std::uint64_t tick = 0, history = 0;
    bool observed = false, aligned = true;
    void reset() { *this = {}; }
    bool ready() const { return observed && aligned; }
    void advance(float dt, bool held) {
        if (dt == 0.f) return; // A zero-duration update advances no physics.
        if (!std::isfinite(dt) || std::abs(dt - .25f) >= .00001f) {
            aligned = false; // Fractional/custom steps cannot be replayed by this model.
            return;
        }
        if (!aligned) return; // Only a new attempt can restore the initial model state.
        observed = true;
        history = (history << 1) | unsigned(held);
        ++tick;
    }
};
}
