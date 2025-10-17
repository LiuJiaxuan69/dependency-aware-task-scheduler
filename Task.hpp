#ifndef _TASK_HPP_
#define _TASK_HPP_ 1
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <semaphore.h>
#include <any>
#include <atomic>
#include "Mutex.hpp"
#include "ConditionVariable.hpp"

enum class Priority : uint8_t {
    LOWEST = 0,
    LOW = 1,
    BELOW_NORMAL = 2,
    NORMAL = 3,
    ABOVE_NORMAL = 4,
    HIGH = 5,
    HIGHEST = 6
};

struct MainTask {
    std::string name;
    Priority priority;
    size_t id; // 任务ID
    size_t subtask_size;
    std::atomic<size_t> completed_subtasks = 0; // 已完成的子任务数量
    std::atomic<size_t> ready_subtasks = 0; // 已就绪的子任务数量
    MainTask(const std::string& n, size_t s, Priority p = Priority::LOWEST)
        : name(n), subtask_size(s), priority(p) {
    }
    // 移动构造函数
    MainTask(MainTask&& other) noexcept
        : name(std::move(other.name)),
          priority(other.priority),
          id(other.id),
          subtask_size(other.subtask_size),
          completed_subtasks(other.completed_subtasks.load()),
          ready_subtasks(other.ready_subtasks.load()) {
        other.id = 0;
        other.subtask_size = 0;
        other.completed_subtasks = 0;
        other.ready_subtasks = 0;
    }
};

struct CompletionEvent {
    Mutex m;
    ConditionVariable cv;
    bool done = false;
};

struct SubTask {
    std::string name;
    std::string main_task_name;
    std::vector<std::string> dependencies; // 依赖的子任务列表
    size_t id; // 子任务ID
    // sem_t semaphore; // 信号量，用于表示子任务的完成状态
    CompletionEvent completion; // 完成事件，依赖方等待它
    std::function<std::any()> task_function;
    size_t main_task_id; // 所属主任务ID1
    // 被依赖的次数
    // size_t depend_count = 0;
    bool completed = false; // 是否已完成
    SubTask(const std::string& n, const std::string& m_name, std::function<std::any()> func = nullptr)
        : name(n), main_task_name(m_name) {
        // sem_init(&semaphore, 0, 0); // 初始化信号量
        task_function = func;
    }
    // 移动构造函数
    SubTask(SubTask&& other) noexcept
        : name(std::move(other.name)),
          main_task_name(std::move(other.main_task_name)),
          dependencies(std::move(other.dependencies)),
          id(other.id),
          completion(),
          task_function(std::move(other.task_function)),
          main_task_id(other.main_task_id),
          completed(other.completed) {
        other.id = 0;
        other.main_task_id = 0;
        other.completed = false;
    }
};

struct SubTaskResult {
    SubTaskResult() { sem_init(&semaphore, 0, 0); }
    ~SubTaskResult() { sem_destroy(&semaphore); }
    SubTaskResult(const SubTaskResult&) = delete;
    SubTaskResult& operator=(const SubTaskResult&) = delete;
    SubTaskResult(SubTaskResult&& other) noexcept
        : result(std::move(other.result)) { semaphore = other.semaphore; }
    SubTaskResult& operator=(SubTaskResult&&) = delete;

    std::any result;
    std::exception_ptr error;
    sem_t semaphore{};
};

#endif