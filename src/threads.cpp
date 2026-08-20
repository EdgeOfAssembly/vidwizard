/**
 * @file threads.cpp
 * @brief Logical-core count and thread pool.
 */

#include "vidwizard/threads.hpp"

namespace vidwizard
{

unsigned logical_core_count(void)
{
    const unsigned n = std::thread::hardware_concurrency();
    return n == 0U ? 1U : n;
}

unsigned resolve_jobs(unsigned jobs)
{
    if (jobs == 0U)
    {
        return logical_core_count();
    }
    return jobs;
}

thread_pool::thread_pool(unsigned n)
{
    const unsigned count = n == 0U ? 1U : n;
    workers_.reserve(count);
    for (unsigned i = 0; i < count; i++)
    {
        workers_.emplace_back([this]() { worker_loop(); });
    }
}

thread_pool::~thread_pool()
{
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    for (std::thread &t : workers_)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
}

void thread_pool::submit(std::function<void()> job)
{
    {
        std::lock_guard<std::mutex> lock(mu_);
        tasks_.push(std::move(job));
    }
    cv_.notify_one();
}

void thread_pool::wait_idle()
{
    std::unique_lock<std::mutex> lock(mu_);
    idle_cv_.wait(lock, [this]() { return tasks_.empty() && busy_ == 0U; });
}

unsigned thread_pool::size() const
{
    return static_cast<unsigned>(workers_.size());
}

void thread_pool::worker_loop()
{
    for (;;)
    {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this]() { return stop_ || !tasks_.empty(); });
            if (stop_ && tasks_.empty())
            {
                return;
            }
            job = std::move(tasks_.front());
            tasks_.pop();
            busy_++;
        }
        job();
        {
            std::lock_guard<std::mutex> lock(mu_);
            busy_--;
            if (tasks_.empty() && busy_ == 0U)
            {
                idle_cv_.notify_all();
            }
        }
    }
}

} // namespace vidwizard
