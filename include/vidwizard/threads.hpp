/**
 * @file threads.hpp
 * @brief Logical-core detection and a small waitable thread pool.
 */
#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace vidwizard
{

/**
 * @brief Number of logical CPUs, or 1 if the host reports 0.
 *
 * @return Hardware concurrency suitable as a default `--jobs` value.
 */
unsigned logical_core_count(void);

/**
 * @brief Resolve `--jobs` (0 means autodetect).
 *
 * @param[in] jobs User value; 0 → logical_core_count().
 *
 * @return At least 1.
 */
unsigned resolve_jobs(unsigned jobs);

/**
 * @brief FIFO worker pool. Jobs run on worker threads; wait_idle() joins work.
 *
 * @thread_safety submit/wait_idle are safe from the owner thread. Submitted
 *                callables must not destroy the pool.
 */
class thread_pool
{
public:
    /**
     * @brief Start @p n workers (clamped to at least 1).
     *
     * @param[in] n Worker count.
     */
    explicit thread_pool(unsigned n);

    thread_pool(const thread_pool &) = delete;
    thread_pool &operator=(const thread_pool &) = delete;

    /**
     * @brief Stop workers after the queue drains.
     */
    ~thread_pool();

    /**
     * @brief Enqueue a job.
     *
     * @param[in] job Callable; moved into the queue.
     */
    void submit(std::function<void()> job);

    /**
     * @brief Block until the queue is empty and no worker is busy.
     */
    void wait_idle();

    /**
     * @brief Worker count.
     *
     * @return Number of threads started in the constructor.
     */
    unsigned size() const;

private:
    void worker_loop();

    std::vector<std::thread> workers_{};
    std::queue<std::function<void()>> tasks_{};
    std::mutex mu_{};
    std::condition_variable cv_{};
    std::condition_variable idle_cv_{};
    bool stop_ = false;
    unsigned busy_ = 0;
};

} // namespace vidwizard
