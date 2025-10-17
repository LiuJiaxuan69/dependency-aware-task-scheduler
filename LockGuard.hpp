#ifndef _LOCKGUARD_HPP_
#define _LOCKGUARD_HPP_ 1
#include <pthread.h>
#include "Mutex.hpp"

class LockGuard {
public:
    explicit LockGuard(Mutex& mutex) : m_mutex(mutex) {
        m_mutex.lock();
    }

    ~LockGuard() {
        m_mutex.unlock();
    }

    // Delete copy constructor and copy assignment operator
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
private:
    Mutex& m_mutex;
};
#endif