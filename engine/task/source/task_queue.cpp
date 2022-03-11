#include "task_queue.hpp"
#include "log.hpp"

namespace ash::task
{
task* task_queue::pop()
{
    task* result = nullptr;
    if (m_queue.pop(result))
        return result;
    else
        return nullptr;
}

void task_queue::push(task* t)
{
    m_queue.push(t);
    m_cv.notify_one();
}

std::future<void> task_queue::push_root_task(task* t)
{
    m_done = std::promise<void>();

    m_remaining_tasks_count.store(static_cast<uint32_t>(t->get_reachable_tasks_size()));
    push(t);

    return m_done.get_future();
}

bool task_queue::empty() const
{
    return m_queue.empty();
}

void task_queue::notify()
{
    m_cv.notify_all();
}

void task_queue::notify_task_completion(bool force)
{
    while (true)
    {
        uint32_t old_value = m_remaining_tasks_count.load();
        uint32_t new_value = force ? 0 : old_value - 1;

        if (old_value == m_remaining_tasks_count.load())
        {
            if (m_remaining_tasks_count.compare_exchange_weak(old_value, new_value))
            {
                if (old_value != 0 && new_value == 0)
                    m_done.set_value();
                break;
            }
        }
    }
}

void task_queue::wait_task(std::function<bool()> exit)
{
    std::unique_lock<std::mutex> ul(m_lock);
    m_cv.wait(ul, [this, &exit]() { return !empty() || exit(); });
}
} // namespace ash::task