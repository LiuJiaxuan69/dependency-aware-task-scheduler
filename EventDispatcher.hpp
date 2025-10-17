#ifndef _EVENTLOOPER_HPP_
#define _EVENTLOOPER_HPP_ 1
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <unistd.h>
#include "Task.hpp"
#include "IDAllocator.hpp"
#include "ThreadPool.hpp"
#include "thread.hpp"
#include "LockGuard.hpp"
#include "Mutex.hpp"
#include "ConditionVariable.hpp"

template <size_t PoolSize = 24>
class EventDispatcher
{
public:
    static EventDispatcher &getInstance()
    {
        static EventDispatcher instance;
        return instance;
    }
    // 封装子任务的任务
    auto wrapSubTask(int subtask_id)
    {
        return [this, subtask_id]()
        {
            std::shared_ptr<SubTask> subtask = nullptr;
            {
                LockGuard lock(sub_tasks_mutex);
                auto it = sub_tasks.find(subtask_id);
                if (it == sub_tasks.end())
                    return;
                subtask = it->second;
            }

            for (const auto &name : subtask->dependencies)
            {
                size_t dep_id = 0;
                {
                    LockGuard lock(sub_task_name_to_id_mutex);
                    auto dep_it = sub_task_name_to_id.find(name);
                    if (dep_it == sub_task_name_to_id.end())
                        continue;
                    dep_id = dep_it->second;
                }
                std::shared_ptr<SubTask> dep_task;
                {
                    LockGuard lock(sub_tasks_mutex);
                    auto dep_it = sub_tasks.find(dep_id);
                    if (dep_it != sub_tasks.end())
                        dep_task = dep_it->second;
                }
                // if (dep_task)
                //     sem_wait(&dep_task->semaphore);
                if (dep_task)
                {
                    UniqueLock lock(dep_task->completion.m);
                    while (!dep_task->completion.done) {
                        dep_task->completion.cv.wait(lock);
                    }
                }
                
            }

            std::any result;
            std::exception_ptr error;
            try {
            if (subtask->task_function) result = subtask->task_function();
        } catch (...) {
            error = std::current_exception();
        }

            {
                LockGuard lock(subtask_results_mutex);
                auto it = subtask_results.find(subtask_id);
                if (it != subtask_results.end()) {
                    auto slot = it->second;
                    slot->result = std::move(result);
                    slot->error = error;
                    sem_post(&slot->semaphore);
                }
            }

            subtask->completed = true;
            subtask->completion.m.lock();
            subtask->completion.done = true;
            subtask->completion.cv.notify_all();
            subtask->completion.m.unlock();
            // for(size_t i = 0; i < subtask->depend_count; ++i)
            //     sem_post(&subtask->semaphore);
            std::shared_ptr<MainTask> main_task;
            {
                LockGuard lock(main_tasks_mutex);
                auto it = main_tasks.find(subtask->main_task_id);
                if (it != main_tasks.end())
                    main_task = it->second;
            }

            if (main_task &&
                ++main_task->completed_subtasks == main_task->subtask_size)
            {
                {
                    LockGuard lock(main_tasks_mutex);
                    main_tasks.erase(main_task->id);
                }
                {
                    LockGuard lock(main_to_subtasks_mutex);
                    main_to_subtasks.erase(main_task->id);
                }
            }
        };
    }
    void start()
    {
        is_running.store(true, std::memory_order_release);
        event_loop_thread = Thread([this]()
                                   {
            while (is_running.load(std::memory_order_acquire) || hasPendingWork())
            {
                EventLooper();
                usleep(1000); // 避免忙等待
            } });
    }
    bool hasPendingWork()
    {
        { // 仍有待调度的子任务
            LockGuard lock(ready_mutex);
            for (const auto &queue : ready_subtasks)
            {
                if (!queue.empty())
                {
                    return true;
                }
            }
        }

        { // 主任务仍在等待子任务就绪
            LockGuard lock(waiting_maintasks_mutex);
            for (const auto &bucket : waiting_maintasks)
            {
                if (!bucket.empty())
                {
                    return true;
                }
            }
        }

        { // 子任务尚未执行完
            LockGuard lock(sub_tasks_mutex);
            for(const auto &entry: sub_tasks)
            {
                if (!entry.second->completed)
                {
                    return true;
                }
            }
        }

        return false;
    }
    // true 表示有任务被分配， false表示没有任务被分配
    void EventLooper()
    {
        // 将准备就绪的子任务按照优先级顺序分配给线程池执行
        LockGuard lock(ready_mutex);
        for (auto& ready_subtask : ready_subtasks)
        {
            while (!ready_subtask.empty())
            {
                int subtask_id = ready_subtask.front();
                ready_subtask.pop();
                auto func = wrapSubTask(subtask_id);
                thread_pool.enqueue(func);
            }
        }
    }
    void addMainTask(MainTask &&task)
    {
        size_t id = main_task_id_allocator.allocate();
        task.id = id;
        {
            LockGuard lock(main_task_name_to_id_mutex);
            main_task_name_to_id[task.name] = id;
        }
        {
            LockGuard lock(waiting_maintasks_mutex);
            waiting_maintasks[static_cast<size_t>(task.priority)].insert(id);
        }
        {
            LockGuard lock(main_tasks_mutex);
            main_tasks.insert({id, std::make_shared<MainTask>(std::move(task))});
        }
    }

    std::any getSubTaskResult(const std::string& subtask_name) {
        size_t subtask_id;
        {
            LockGuard lock(sub_task_name_to_id_mutex);
            auto it = sub_task_name_to_id.find(subtask_name);
            if (it == sub_task_name_to_id.end()) {
                throw std::runtime_error("Subtask not found for name: " + subtask_name);
            }
            subtask_id = it->second;
        }

        std::shared_ptr<SubTaskResult> slot;
        {
            LockGuard lock(subtask_results_mutex);
            auto it = subtask_results.find(subtask_id);
            if (it == subtask_results.end()) {
                throw std::runtime_error("Subtask result not found for ID: " + std::to_string(subtask_id));
            }
            slot = it->second;
        }

        sem_wait(&slot->semaphore);

        std::any result;
        std::exception_ptr error;
        {
            LockGuard lock(subtask_results_mutex);
            result = std::move(slot->result);
            error = slot->error;
            subtask_results.erase(subtask_id);
        }
        {
            LockGuard lock(sub_task_name_to_id_mutex);
            sub_task_name_to_id.erase(subtask_name);
        }
        {
            LockGuard lock(sub_tasks_mutex);
            auto it = sub_tasks.find(subtask_id);
            if (it != sub_tasks.end())
            {
                sub_tasks.erase(it);
            }
        }
        if (error) std::rethrow_exception(error);
        return result;
    }

    void addSubTask(SubTask &&task)
    {
        size_t id = sub_task_id_allocator.allocate();
        task.id = id;

        size_t main_id = 0;
        {
            LockGuard lock(main_task_name_to_id_mutex);
            auto it = main_task_name_to_id.find(task.main_task_name);
            if (it == main_task_name_to_id.end()) {
                throw std::runtime_error("Main task not found for subtask: " + task.name);
            }
            main_id = it->second;
        }
        // if (main_it != main_task_name_to_id.end())
        // {
        //     size_t main_id = main_it->second;
            task.main_task_id = main_id;
            // {
            //     LockGuard lock(main_tasks_mutex);
            //     task.depend_count = main_tasks.find(main_id)->second->subtask_size - 1;
            // }
            {
                LockGuard lock(sub_task_name_to_id_mutex);
                sub_task_name_to_id[task.name] = id;
            }
            {
                LockGuard lock(sub_tasks_mutex);
                sub_tasks.insert({id, std::make_shared<SubTask>(std::move(task))});
            }
            {
                LockGuard lock(main_to_subtasks_mutex);
                main_to_subtasks[main_id].push_back(id);
            }
            {
                LockGuard lock(subtask_results_mutex);
                subtask_results.emplace(id, std::make_shared<SubTaskResult>());
            }
            // 初始化依赖状态
            main_tasks_mutex.lock();
            std::shared_ptr<MainTask> main_task = main_tasks.find(main_id)->second;
            main_tasks_mutex.unlock();
            if (++main_task->ready_subtasks == main_task->subtask_size)
            {
                // 所有子任务就绪，将所有子任务提交到子任务就绪队列当中并将主任务出队
                {
                    LockGuard lock(ready_mutex);
                    for (size_t sub_id : main_to_subtasks[main_id])
                    {
                        ready_subtasks[static_cast<size_t>(main_task->priority)].push(sub_id);
                    }
                    {
                        LockGuard lock(waiting_maintasks_mutex);
                        waiting_maintasks[static_cast<size_t>(main_task->priority)].erase(main_id);
                    }
                }
            }
        // }
        // else
        // {
        //     throw std::runtime_error("Main task not found for subtask: " + task.name);
        // }
    }
    void Stop()
    {
        is_running.store(false, std::memory_order_release);
        if(event_loop_thread.joinable())
        event_loop_thread.join();
    }

private:
    EventDispatcher() {};
    ~EventDispatcher()
    {
        Stop();
    };
    EventDispatcher(const EventDispatcher &) = delete;
    EventDispatcher &operator=(const EventDispatcher &) = delete;

private:
    Thread event_loop_thread;
    IDAllocator main_task_id_allocator;
    IDAllocator sub_task_id_allocator;
    std::unordered_map<std::string, size_t> main_task_name_to_id;                             // 主任务名称到ID的映射
    std::unordered_map<size_t, std::shared_ptr<MainTask>> main_tasks;                         // 主任务ID到主任务的映射
    std::unordered_map<size_t, std::vector<size_t>> main_to_subtasks;                         // 主任务ID到其子任务ID列表的映射
    std::unordered_map<std::string, size_t> sub_task_name_to_id;                              // 子任务名称到ID的映射
    std::unordered_map<size_t, std::shared_ptr<SubTask>> sub_tasks;                           // 子任务ID到子任务的映射
    std::unordered_map<size_t, std::shared_ptr<SubTaskResult>> subtask_results;               // 子任务ID到其结果的映射
    std::unordered_set<size_t> waiting_maintasks[static_cast<size_t>(Priority::HIGHEST) + 1]; // 等待中的主任务ID set 表(等待所有子任务就绪)
    std::queue<size_t> ready_subtasks[static_cast<size_t>(Priority::HIGHEST) + 1];            // 准备就绪的子任务ID队列
    ThreadPool<PoolSize> thread_pool;                                                         // 线程池

    Mutex main_task_name_to_id_mutex; // 保护main_task_name_to_id的互斥锁
    Mutex ready_mutex;                // 保护ready_subtasks的互斥锁
    Mutex sub_tasks_mutex;            // 保护sub_tasks的互斥锁
    Mutex main_tasks_mutex;           // 保护main_tasks的互斥锁
    Mutex main_to_subtasks_mutex;     // 保护main_to_subtasks的互斥锁
    Mutex sub_task_name_to_id_mutex;  // 保护sub_task_name_to_id的互斥锁
    Mutex subtask_results_mutex;      // 保护subtask_results的互斥锁
    Mutex waiting_maintasks_mutex;    // 保护waiting_maintasks的互斥锁

    std::atomic<bool> is_running;
};
#endif