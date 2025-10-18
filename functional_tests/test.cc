#include <iostream>
#include <future>
#include <memory>
#include <mutex>
#include <vector>
#include <atomic>
#include "EventDispatcher.hpp"
#include "ThreadPool.hpp"
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <filesystem>
#include <random>
#include <chrono>
#include <thread>

constexpr size_t thread_pool_size = 50;

class DispatchEventTest : public ::testing::Test {
protected:
    void SetUp() override {
        dispathcer = &EventDispatcher<thread_pool_size>::getInstance();
        dispathcer->start();
    }

    void TearDown() override {
        dispathcer->Stop();
    }

    EventDispatcher<thread_pool_size>* dispathcer{nullptr};
};

TEST_F(DispatchEventTest, HandlesMixedReturnValues) {
    MainTask main_task("MainTaskMixed", 3, Priority::HIGH);
    dispathcer->addMainTask(std::move(main_task));

    SubTask subtask1("Mixed_Task1", "MainTaskMixed", []() -> std::any {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return 42;
    });
    SubTask subtask2("Mixed_Task2", "MainTaskMixed", []() -> std::any {
        return std::string("ResultFromTask2");
    });
    SubTask subtask3("Mixed_Task3", "MainTaskMixed", []() -> std::any {
        return std::vector<int>{1, 2, 3, 4};
    });

    dispathcer->addSubTask(std::move(subtask1));
    dispathcer->addSubTask(std::move(subtask2));
    dispathcer->addSubTask(std::move(subtask3));

    auto result1 = std::any_cast<int>(dispathcer->getSubTaskResult("Mixed_Task1"));
    auto result2 = std::any_cast<std::string>(dispathcer->getSubTaskResult("Mixed_Task2"));
    auto result3 = std::any_cast<std::vector<int>>(dispathcer->getSubTaskResult("Mixed_Task3"));

    EXPECT_EQ(result1, 42);
    EXPECT_EQ(result2, "ResultFromTask2");
    EXPECT_EQ(result3.size(), 4U);
    EXPECT_EQ(result3[0], 1);
}

// ... Keep the remainder of tests identical ...
