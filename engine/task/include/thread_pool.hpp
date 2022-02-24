#pragma once

#include "work_thread.hpp"

namespace ash::task
{
class thread_pool
{
public:
    thread_pool(std::size_t num_thread);
    thread_pool(const thread_pool&) = delete;
    thread_pool(thread_pool&&) = default;
    ~thread_pool();

    void run(task_queue& queue);
    void stop();

    thread_pool& operator=(const thread_pool&) = delete;

private:
    std::vector<work_thread> m_threads;
    task_queue* m_queue;
};
} // namespace ash::task