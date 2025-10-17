#ifndef _RING_QUEUE_HPP_
#define _RING_QUEUE_HPP_ 1
#include <iostream>
#include <vector>
#include <semaphore.h>
#include "LockGuard.hpp"
#include "Mutex.hpp"

template<typename Task, size_t Size>
class RingQueue {
public:
    RingQueue() : Tasks(Size), head(0), tail(0), stop(false) {
        sem_init(&empty, 0, 0); // 初始时没有任务
        sem_init(&full, 0, Size); // 初始时所有槽位都是
    }
    ~RingQueue() {
        sem_destroy(&empty);
        sem_destroy(&full);
    }
    template<typename F>
    bool emplace(F&& f) {
        P(full);
        {
            LockGuard lock(pro_mutex);
            if(stop) {
                V(full);    // 放回信号量
                return false;
            }
            size_t next_tail = (tail + 1) % Size;
            Tasks[tail] = std::forward<F>(f);
            tail = next_tail;
        }
        V(empty);
        return true;
    }
    bool pop(Task& task) {
        P(empty);
        {
            LockGuard lock(con_mutex);
            if(stop && IsEmpty()) {
                V(empty);   // 放回信号量
                return false;
            }
            task = std::move(Tasks[head]);
            head = (head + 1) % Size;
        }
        V(full);
        return true;
    }
    // 强制停止，谨慎使用
    void Stop() {
        {
            LockGuard lock(pro_mutex);
            LockGuard lock2(con_mutex);
            stop = true;
        }
        // 释放所有等待的线程
        for(size_t i = 0; i < Size; ++i) {
            V(empty);
            V(full);
        }
    }
    bool IsEmpty() const {
        return head == tail;
    }
private:
    void P(sem_t &sem) {
        if(sem_wait(&sem) != 0) {
            throw std::runtime_error("sem_wait failed");
        }
    }
    void V(sem_t &sem) {
        if(sem_post(&sem) != 0) {
            throw std::runtime_error("sem_post failed");
        }
    }
private:
    std::vector<Task> Tasks;
    Mutex pro_mutex; // 生产者互斥锁
    Mutex con_mutex; // 消费者互斥锁
    size_t head; // 任务队列头（下一个要处理的任务）
    size_t tail; // 任务队列尾 （下一个可插入任务的位置）
    sem_t empty; // 空槽位信号量
    sem_t full;  // 满槽位信号量
    bool stop; // 停止标志
};
#endif // _RING_QUEUE_HPP_