#ifndef _MUTEX_HPP_
#define _MUTEX_HPP_ 1
#include <semaphore.h>
#include <system_error>


class Mutex {
private:
    sem_t semaphore_;
    bool is_locked_;

public:
    // 构造函数
    Mutex() : is_locked_(false) {
        // 初始化信号量，初始值为1（可用）
        if (sem_init(&semaphore_, 0, 1) != 0) {
            throw std::system_error(errno, std::system_category(), "Failed to initialize semaphore");
        }
    }

    // 析构函数
    ~Mutex() {
        sem_destroy(&semaphore_);
    }

    // 禁止拷贝
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    // 加锁
    void lock() {
        if (sem_wait(&semaphore_) != 0) {
            throw std::system_error(errno, std::system_category(), "Failed to lock mutex");
        }
        is_locked_ = true;
    }

    // 尝试加锁
    bool try_lock() {
        int result = sem_trywait(&semaphore_);
        if (result == 0) {
            is_locked_ = true;
            return true;
        } else if (errno == EAGAIN) {
            // 信号量不可用
            return false;
        } else {
            throw std::system_error(errno, std::system_category(), "Failed to try lock mutex");
        }
    }

    // 解锁
    void unlock() {
        if (sem_post(&semaphore_) != 0) {
            throw std::system_error(errno, std::system_category(), "Failed to unlock mutex");
        }
        is_locked_ = false;
    }

    // 检查是否已锁定
    bool is_locked() const {
        return is_locked_;
    }
};

#endif