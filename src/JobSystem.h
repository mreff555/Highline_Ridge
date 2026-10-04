/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Portable CPU job pool for game + tools. Workers never touch raylib GL.
 * Completions are drained on the main thread via pollCompletions().
 ******************************************************************************/

#ifndef TIMBERLINE_JOB_SYSTEM_H
#define TIMBERLINE_JOB_SYSTEM_H

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace timberline_engine
{

using JobFn = std::function<void()>;
using JobId = std::uint64_t;

/**
 * Fixed-size worker pool + main-thread completion queue.
 *
 * Typical game use:
 *   jobs.enqueue(decodeWork, [&]{ uploadOnMain(); });
 *   // each frame:
 *   jobs.pollCompletions();
 */
class JobSystem
{
public:
    /** workerCount 0 → max(1, hardware_concurrency - 1). */
    explicit JobSystem(unsigned workerCount = 0);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    unsigned workerCount() const { return static_cast<unsigned>(workers_.size()); }

    /** Outstanding work + undrained completions. */
    std::size_t pendingCount() const;

    /**
     * Enqueue CPU work. Optional onComplete runs later on the thread that
     * calls pollCompletions() (normally the main / GL thread).
     */
    JobId enqueue(JobFn work, JobFn onComplete = JobFn{});

    /** Drain up to maxToRun completion callbacks on the calling thread. */
    void pollCompletions(std::size_t maxToRun = static_cast<std::size_t>(-1));

    /** Block until the work queue is empty and all workers are idle. */
    void waitIdle();

    /** Process-wide instance used by the game (created lazily). */
    static JobSystem& global();
    static void shutdownGlobal();

private:
    struct Job
    {
        JobId id = 0;
        JobFn work;
        JobFn onComplete;
    };

    void workerMain();
    void stopWorkers();

    mutable std::mutex mutex_;
    std::condition_variable cvWork_;
    std::condition_variable cvIdle_;
    std::queue<Job> workQueue_;
    std::queue<JobFn> completionQueue_;
    std::vector<std::thread> workers_;
    std::atomic<bool> stopping_{false};
    std::atomic<std::uint64_t> nextId_{1};
    std::size_t activeWorkers_ = 0; // holding mutex_
};

} // namespace timberline_engine

#endif /* TIMBERLINE_JOB_SYSTEM_H */
