#ifndef _UNIQUE_LOCK_HPP_
#define _UNIQUE_LOCK_HPP_ 1
#include "Mutex.hpp"

class UniqueLock {
public:
    explicit UniqueLock(Mutex& mutex) : m_mutex(mutex), owns_lock(false) {
        lock();
    }

    ~UniqueLock() {
        unlock();
    }

    void lock() {
        if (!owns_lock) {
            m_mutex.lock();
            owns_lock = true;
        }
    }

    void unlock() {
        if (owns_lock) {
            m_mutex.unlock();
            owns_lock = false;
        }
    }

private:
    Mutex& m_mutex;
    bool owns_lock;
};

#endif