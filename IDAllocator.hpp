#ifndef _ID_ALLOCATOR_HPP_
#define _ID_ALLOCATOR_HPP_ 1

#include <iostream>
#include <queue>
#include <unordered_set>
#include <atomic>
#include <optional>
#include <pthread.h>
#include "LockGuard.hpp"
#include "Mutex.hpp"

class IDAllocator {
private:
    std::queue<int> recycled_ids;      // 回收的ID队列
    std::unordered_set<int> used_ids;  // 当前正在使用的ID（用于去重）
    Mutex mutex; // 保护数据结构的互斥锁
    std::atomic<int> next_id{1};       // 下一个可分配的新ID

public:
    // 构造函数
    IDAllocator() = default;
    
    // 禁止拷贝
    IDAllocator(const IDAllocator&) = delete;
    IDAllocator& operator=(const IDAllocator&) = delete;
    
    /**
     * 申请一个ID
     * @return 分配到的ID
     */
    size_t allocate() {
        LockGuard lock(mutex);
        
        // 优先从回收队列中获取ID
        if (!recycled_ids.empty()) {
            int id = recycled_ids.front();
            recycled_ids.pop();
            used_ids.insert(id);
            // std::cout << "Allocated recycled ID: " << id << std::endl;
            return id;
        }
        
        // 分配新ID
        int new_id = next_id++;
        used_ids.insert(new_id);
        // std::cout << "Allocated new ID: " << new_id << std::endl;
        return new_id;
    }
    
    /**
     * 归还ID
     * @param id 要归还的ID
     * @return 是否成功归还
     */
    bool deallocate(int id) {
        LockGuard lock(mutex);
        
        // 检查ID是否正在使用
        if (used_ids.find(id) == used_ids.end()) {
            // std::cout << "Warning: Trying to deallocate unused ID: " << id << std::endl;
            return false;
        }
        
        // 从使用集合中移除
        used_ids.erase(id);
        
        // 添加到回收队列
        recycled_ids.push(id);
        
        // std::cout << "Deallocated ID: " << id << std::endl;
        return true;
    }
    
    /**
     * 批量申请多个ID
     * @param count 申请数量
     * @return 分配到的ID列表
     */
    std::vector<int> allocateBatch(int count) {
        std::vector<int> ids;
        ids.reserve(count);

        LockGuard lock(mutex);

        for (int i = 0; i < count; ++i) {
            // 优先使用回收的ID
            if (!recycled_ids.empty()) {
                int id = recycled_ids.front();
                recycled_ids.pop();
                used_ids.insert(id);
                ids.push_back(id);
            } else {
                // 分配新ID
                int new_id = next_id++;
                used_ids.insert(new_id);
                ids.push_back(new_id);
            }
        }
        
        return ids;
    }
    
    /**
     * 批量归还多个ID
     * @param ids 要归还的ID列表
     */
    void deallocateBatch(const std::vector<int>& ids) {
        LockGuard lock(mutex);
        
        for (int id : ids) {
            if (used_ids.find(id) != used_ids.end()) {
                used_ids.erase(id);
                recycled_ids.push(id);
            }
        }
    }
    
    /**
     * 检查ID是否正在使用
     */
    bool isInUse(int id) {
        LockGuard lock(mutex);
        return used_ids.find(id) != used_ids.end();
    }
    
    /**
     * 获取当前正在使用的ID数量
     */
    size_t getUsedCount() {
        LockGuard lock(mutex);
        return used_ids.size();
    }
    
    /**
     * 获取可回收的ID数量
     */
    size_t getRecyclableCount() {
        LockGuard lock(mutex);
        return recycled_ids.size();
    }
    
    /**
     * 获取下一个将要分配的新ID
     */
    int getNextNewID() const {
        return next_id.load();
    }
    
    /**
     * 重置分配器（清空所有状态）
     * 注意：这会使得已分配的ID无效，请谨慎使用！
     */
    void reset(int start_id = 1) {
        LockGuard lock(mutex);
        recycled_ids = std::queue<int>();
        used_ids.clear();
        next_id = start_id;
        std::cout << "ID Allocator reset, next ID: " << start_id << std::endl;
    }
};

#endif // _ID_ALLOCATOR_HPP_