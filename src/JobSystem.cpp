/*******************************************************************************
 * Timberline engine
 * Copyright (C) 2026 Dan Feerst
 *
 * Portable CPU job pool for game + tools.
 ******************************************************************************/

#include "JobSystem.h"

#include <algorithm>
#include <utility>

namespace timberline_engine
{
namespace
{

JobSystem* gGlobalJobs = nullptr;

unsigned resolveWorkerCount(unsigned requested)
{
    if (requested > 0)
        return requested;
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    // Leave one logical core for the main / GL thread.
    return std::max(1u, hw - 1u);
}

} // namespace

JobSystem::JobSystem(unsigned workerCount)
{
    const unsigned n = resolveWorkerCount(workerCount);
    workers_.reserve(n);
    for (unsigned i = 0; i < n; ++i)
        workers_.emplace_back([this]() { workerMain(); });
}

JobSystem::~JobSystem()
{
    stopWorkers();
}

void JobSystem::stopWorkers()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_.store(true, std::memory_order_release);
    }
    cvWork_.notify_all();
    for (std::thread& t : workers_)
    {
        if (t.joinable())
            t.join();
    }
    workers_.clear();
}

std::size_t JobSystem::pendingCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return workQueue_.size() + activeWorkers_ + completionQueue_.size();
}

JobId JobSystem::enqueue(JobFn work, JobFn onComplete)
{
    if (!work)
        return 0;

    Job job;
    job.id = nextId_.fetch_add(1, std::memory_order_relaxed);
    job.work = std::move(work);
    job.onComplete = std::move(onComplete);
    const JobId id = job.id;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_.load(std::memory_order_acquire))
            return 0;
        workQueue_.push(std::move(job));
    }
    cvWork_.notify_one();
    return id;
}

void JobSystem::pollCompletions(std::size_t maxToRun)
{
    std::size_t ran = 0;
    while (ran < maxToRun)
    {
        JobFn cb;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (completionQueue_.empty())
                break;
            cb = std::move(completionQueue_.front());
            completionQueue_.pop();
        }
        if (cb)
            cb();
        ++ran;
    }
}

void JobSystem::waitIdle()
{
    std::unique_lock<std::mutex> lock(mutex_);
    cvIdle_.wait(lock, [this]() {
        return workQueue_.empty() && activeWorkers_ == 0;
    });
}

void JobSystem::workerMain()
{
    for (;;)
    {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cvWork_.wait(lock, [this]() {
                return stopping_.load(std::memory_order_acquire) || !workQueue_.empty();
            });
            if (stopping_.load(std::memory_order_acquire) && workQueue_.empty())
                return;
            job = std::move(workQueue_.front());
            workQueue_.pop();
            ++activeWorkers_;
        }

        if (job.work)
            job.work();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (job.onComplete)
                completionQueue_.push(std::move(job.onComplete));
            --activeWorkers_;
            if (workQueue_.empty() && activeWorkers_ == 0)
                cvIdle_.notify_all();
        }
    }
}

JobSystem& JobSystem::global()
{
    if (gGlobalJobs == nullptr)
        gGlobalJobs = new JobSystem();
    return *gGlobalJobs;
}

void JobSystem::shutdownGlobal()
{
    delete gGlobalJobs;
    gGlobalJobs = nullptr;
}

} // namespace timberline_engine
