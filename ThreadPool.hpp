#ifndef _THREAD_POOL_HPP_
#define _THREAD_POOL_HPP_ 1

#include <iostream>
#include <vector>
#include <queue>
#include <functional>
#include <future>
#include "thread.hpp"
#include "RingQueue.hpp"

template <size_t N>
class ThreadPool
{
public:
    ThreadPool()
    {
        for (size_t i = 0; i < N; ++i)
        {
            workers.emplace_back([this]() {
                std::function<void()> task;
                while (tasks.pop(task))
                {
                    try {
                        task();
                    } catch (...) {
                        // 处理任务中的异常(do nothing)
                    }
                }
            });
        }
    }

    ~ThreadPool()
    {
        tasks.Stop();
        for (Thread &worker : workers)
            worker.join();
    }

    template <class F, class... Args>
    auto enqueue(F &&f, Args &&...args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using return_type = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<return_type> res = task->get_future();
        if (!tasks.emplace([task]()
                           { (*task)(); }))
        {
            throw std::runtime_error("enqueue on stopped ThreadPool");
        }
        return res;
    }

private:
    // 线程池成员变量
    std::vector<Thread> workers;
    RingQueue<std::function<void()>, N> tasks;
};

#endif // _THREAD_POOL_HPP_