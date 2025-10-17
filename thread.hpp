#ifndef THREAD_H
#define THREAD_H

#include <pthread.h>
#include <functional>
#include <memory>
#include <utility>

class Thread {
public:
    // 线程函数类型
    using Function = std::function<void()>;
    
    // 默认构造函数
    Thread() : m_thread(0), m_joined(false), m_detached(false) {}
    
    // 构造函数，接受可调用对象
    template<typename Callable, typename... Args>
    explicit Thread(Callable&& func, Args&&... args) 
        : m_thread(0), m_joined(false), m_detached(false) {
        
        // 使用完美转发包装函数和参数
        auto task = std::make_shared<Function>(
            std::bind(std::forward<Callable>(func), std::forward<Args>(args)...)
        );
        
        // 创建线程
        int result = pthread_create(&m_thread, nullptr, &Thread::threadEntry, task.get());
        if (result != 0) {
            throw std::runtime_error("Failed to create thread");
        }
        
        // 保存任务对象，防止提前销毁
        m_task = task;
    }
    
    // 移动构造函数
    Thread(Thread&& other) noexcept 
        : m_thread(other.m_thread),
          m_task(std::move(other.m_task)),
          m_joined(other.m_joined),
          m_detached(other.m_detached) {
        other.m_thread = 0;
        other.m_joined = false;
        other.m_detached = false;
    }
    
    // 移动赋值运算符
    Thread& operator=(Thread&& other) noexcept {
        if (this != &other) {
            if (joinable()) {
                std::terminate();
            }
            
            m_thread = other.m_thread;
            m_task = std::move(other.m_task);
            m_joined = other.m_joined;
            m_detached = other.m_detached;
            
            other.m_thread = 0;
            other.m_joined = false;
            other.m_detached = false;
        }
        return *this;
    }
    
    // 禁止拷贝
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    
    // 析构函数
    ~Thread() {
        if (joinable()) {
            std::terminate();
        }
    }
    
    // 等待线程结束
    void join() {
        if (!joinable()) {
            throw std::runtime_error("thread is not joinable");
        }
        if (m_joined || m_detached) {
            throw std::runtime_error("thread already joined or detached");
        }
        
        int result = pthread_join(m_thread, nullptr);
        if (result != 0) {
            throw std::runtime_error("Failed to join thread");
        }
        m_joined = true;
    }
    
    // 分离线程
    void detach() {
        if (!joinable()) {
            throw std::runtime_error("thread is not joinable");
        }
        if (m_joined || m_detached) {
            throw std::runtime_error("thread already joined or detached");
        }
        
        int result = pthread_detach(m_thread);
        if (result != 0) {
            throw std::runtime_error("Failed to detach thread");
        }
        m_detached = true;
    }
    
    // 检查线程是否可join
    bool joinable() const {
        return m_thread != 0 && !m_joined && !m_detached;
    }
    
    // 获取线程ID
    pthread_t native_handle() {
        return m_thread;
    }
    
    // 交换两个线程对象
    void swap(Thread& other) noexcept {
        std::swap(m_thread, other.m_thread);
        std::swap(m_task, other.m_task);
        std::swap(m_joined, other.m_joined);
        std::swap(m_detached, other.m_detached);
    }
    
    // 静态函数：获取当前线程ID
    static pthread_t get_id() {
        return pthread_self();
    }
    
    // 静态函数：让出CPU
    static void yield() {
        sched_yield();
    }
    
    // 静态函数：睡眠当前线程
    static void sleep_for(long milliseconds) {
        struct timespec ts;
        ts.tv_sec = milliseconds / 1000;
        ts.tv_nsec = (milliseconds % 1000) * 1000000;
        nanosleep(&ts, nullptr);
    }

private:
    // 线程入口函数(用于 pthread_create 函数统一包装函数体，且处理异常)
    static void* threadEntry(void* arg) {
        Function* func = static_cast<Function*>(arg);
        try {
            (*func)();
        } catch (...) {
            // 捕获所有异常，防止线程异常终止
            std::terminate();
        }
        return nullptr;
    }
    
    pthread_t m_thread;                       // 线程句柄
    std::shared_ptr<Function> m_task;         // 任务对象
    bool m_joined;                           // 是否已join
    bool m_detached;                         // 是否已detach
};

// 交换两个线程对象的全局函数
void swap(Thread& lhs, Thread& rhs) noexcept {
    lhs.swap(rhs);
}

#endif // THREAD_H