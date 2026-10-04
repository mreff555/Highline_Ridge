/*******************************************************************************
 * Timberline engine — JobSystem smoke test
 ******************************************************************************/

#include "JobSystem.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

int main()
{
    using timberline_engine::JobSystem;

    JobSystem jobs(4);
    std::atomic<int> worked{0};
    std::atomic<int> completed{0};

    constexpr int kN = 64;
    for (int i = 0; i < kN; ++i)
    {
        jobs.enqueue(
            [&worked]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                worked.fetch_add(1, std::memory_order_relaxed);
            },
            [&completed]() {
                completed.fetch_add(1, std::memory_order_relaxed);
            });
    }

    jobs.waitIdle();

    // Completions are queued by workers; drain on "main".
    for (int spins = 0; spins < 1000 && completed.load() < kN; ++spins)
    {
        jobs.pollCompletions();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const int w = worked.load();
    const int c = completed.load();
    std::printf(
        "JobSystem smoke: workers=%u worked=%d completed=%d pending=%zu\n",
        jobs.workerCount(),
        w,
        c,
        jobs.pendingCount());

    if (w != kN || c != kN)
    {
        std::fprintf(stderr, "FAIL: expected %d/%d\n", kN, kN);
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
