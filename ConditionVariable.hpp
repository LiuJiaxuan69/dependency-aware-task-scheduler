#ifndef CONDITION_VARIABLE_H
#define CONDITION_VARIABLE_H

#include <pthread.h>
#include <queue>
#include <system_error>
#include <semaphore.h>
#include "Mutex.hpp"
#include "UniqueLock.hpp"

class ConditionVariable {
public:
    ConditionVariable() {
        if (sem_init(&m_semaphore, 0, 0) != 0) {
            throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
        }
        m_waiting_count = 0;
    }
    
    ~ConditionVariable() {
        sem_destroy(&m_semaphore);
    }
    
    // 禁止拷贝
    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;
    
    void notify_one() noexcept {
        m_internal_mutex.lock();
        if (m_waiting_count > 0) {
            sem_post(&m_semaphore);
            m_waiting_count--;
        }
        m_internal_mutex.unlock();
    }
    
    void notify_all() noexcept {
        m_internal_mutex.lock();
        while (m_waiting_count > 0) {
            sem_post(&m_semaphore);
            m_waiting_count--;
        }
        m_internal_mutex.unlock();
    }
    
    template<typename Predicate>
    void wait(UniqueLock& lock, Predicate pred) {
        while (!pred()) {
            wait(lock);
        }
    }

    void wait(UniqueLock& lock) {
        // 先登记等待者，避免丢通知
        m_internal_mutex.lock();
        ++m_waiting_count;
        m_internal_mutex.unlock();

        lock.unlock();
        sem_wait(&m_semaphore);
        lock.lock();
    }

private:
    sem_t m_semaphore;              // 核心信号量
    Mutex m_internal_mutex; // 保护内部状态的互斥锁
    unsigned int m_waiting_count;    // 当前等待的线程数
};

#endif // CONDITION_VARIABLE_H