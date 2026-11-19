#include "../worker/branch.cpp"
#include <stdexcept>
void require(bool ok) { if (!ok) throw std::runtime_error("deadline invariant"); }
int main() {
    gd::Level level;
    level.settings.start_speed = 0.9;
    level.settings.spawn_y = 105;
    level.settings.max_gameplay_y = 100000;
    level.deadlineActive = true;
    level.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    bool cancelled = false;
    try { level.timeline(-1); } catch (gd::DeadlineExceeded const&) { cancelled = true; }
    require(cancelled && level.timelines.empty());
    level.deadlineActive = false;
    auto player = gd::Simulator(level).initial_state();
    iax::Decision output{};
    int result = iax_predict_v2(&level, &player, 1, 6000, 4, 0, 0, &output);
    require((result == 0 || result == 2) && !level.deadlineActive);
    // Backoff affects speculative availability only, never unbudgeted real replay.
    int log = player.speed_log;
    level.timelineRetryAfter[log] = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    level.deadlineActive = true;
    level.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    cancelled = false;
    try { level.timeline(log); } catch (gd::DeadlineExceeded const&) { cancelled = true; }
    require(cancelled && level.timelines.empty());
    level.deadlineActive = false;
    require(level.timeline(log) != nullptr && level.timelineRetryAfter.count(log) == 0);
    // A cancelled query must not leave a deadline active across the C ABI replay call.
    auto next = gd::Simulator(level).step(player, false);
    require(next.tick == 1);
}
