#include <iostream>
#include <future>
#include <memory>
#include "EventDispatcher.hpp"

constexpr size_t thread_pool_size = 50;

int main() {
    EventDispatcher<thread_pool_size>& dispathcer = EventDispatcher<thread_pool_size>::getInstance();
    dispathcer.start();
    // 添加主任务和子任务的代码示例
    // 创建主任务
    dispathcer.addMainTask({"MainTask1", 5, Priority::HIGH});
    // 创建 5 个子任务
    auto subtask1 = SubTask("SubTask1", "MainTask1", []() {
        std::cout << "Executing SubTask1" << std::endl;
        return 42; // 示例返回值
    });
    auto subtask2 = SubTask("SubTask2", "MainTask1", []() {
        std::cout << "Executing SubTask2" << std::endl;
        return std::string("Hello from SubTask2"); // 示例返回值
    });
    auto subtask3 = SubTask("SubTask3", "MainTask1", []() {
        std::cout << "Executing SubTask3" << std::endl;
        return 3.14; // 示例返回值
    });
    auto subtask4 = SubTask("SubTask4", "MainTask1", []() {
        std::cout << "Executing SubTask4" << std::endl;
        return 100; // 示例返回值
    });
    auto subtask5 = SubTask("SubTask5", "MainTask1", []() {
        std::cout << "Executing SubTask5" << std::endl;
        return std::string("Hello from SubTask5"); // 示例返回值
    });
    // 创建依赖关系
    subtask2.dependencies = {"SubTask1"}; // SubTask2 依赖 SubTask1
    subtask4.dependencies = {"SubTask1"}; // SubTask4 依赖 SubTask1
    subtask3.dependencies = {"SubTask2", "SubTask4"}; // SubTask3 依赖 SubTask2 和 SubTask4
    subtask5.dependencies = {"SubTask3", "SubTask4"}; // SubTask5 依赖 SubTask3 和 SubTask4

    dispathcer.addMainTask({"MainTask2", 5, Priority::HIGHEST});
    auto subtask6 = SubTask("SubTask6", "MainTask2", []() {
        std::cout << "Executing SubTask6" << std::endl;
        return 256; // 示例返回值
    });
    auto subtask7 = SubTask("SubTask7", "MainTask2", []() {
        std::cout << "Executing SubTask7" << std::endl;
        return std::string("Hello from SubTask7"); // 示例返回值
    });
    auto subtask8 = SubTask("SubTask8", "MainTask2", []() {
        std::cout << "Executing SubTask8" << std::endl;
        return 6.28; // 示例返回值
    });
    auto subtask9 = SubTask("SubTask9", "MainTask2", []() {
        std::cout << "Executing SubTask9" << std::endl;
        return 512; // 示例返回值
    });
    auto subtask10 = SubTask("SubTask10", "MainTask2", []() {
        std::cout << "Executing SubTask10" << std::endl;
        return std::string("Hello from SubTask10"); // 示例返回值
    });
    subtask7.dependencies = {"SubTask6"}; // SubTask7 依赖 SubTask6
    subtask9.dependencies = {"SubTask6"}; // SubTask9 依赖 SubTask6
    subtask8.dependencies = {"SubTask7", "SubTask9"}; // SubTask8 依赖 SubTask7 和 SubTask9
    subtask10.dependencies = {"SubTask8", "SubTask9"}; // SubTask10 依赖 SubTask8 和 SubTask9
    dispathcer.addSubTask(std::move(subtask5));
    dispathcer.addSubTask(std::move(subtask2));
    dispathcer.addSubTask(std::move(subtask4));
    dispathcer.addSubTask(std::move(subtask3));
    dispathcer.addSubTask(std::move(subtask10));
    dispathcer.addSubTask(std::move(subtask1));
    dispathcer.addSubTask(std::move(subtask8));
    dispathcer.addSubTask(std::move(subtask6));
    dispathcer.addSubTask(std::move(subtask7));
    dispathcer.addSubTask(std::move(subtask9));
    auto result1 = dispathcer.getSubTaskResultAs<int>("SubTask1"); // 获取 SubTask1 的结果
    auto result2 = dispathcer.getSubTaskResultAs<std::string>("SubTask2"); // 获取 SubTask2 的结果
    auto result3 = dispathcer.getSubTaskResultAs<double>("SubTask3"); // 获取 SubTask3 的结果
    auto result4 = dispathcer.getSubTaskResultAs<int>("SubTask4"); // 获取 SubTask4 的结果
    auto result5 = dispathcer.getSubTaskResultAs<std::string>("SubTask5"); // 获取 SubTask5
    auto result6 = dispathcer.getSubTaskResultAs<int>("SubTask6"); // 获取 SubTask6 的结果
    auto result7 = dispathcer.getSubTaskResultAs<std::string>("SubTask7"); // 获取 SubTask7 的结果
    auto result8 = dispathcer.getSubTaskResultAs<double>("SubTask8"); // 获取 SubTask8 的结果
    auto result9 = dispathcer.getSubTaskResultAs<int>("SubTask9"); // 获取 SubTask9 的结果
    auto result10 = dispathcer.getSubTaskResultAs<std::string>("SubTask10"); // 获取 SubTask10 的结果
    std::cout << "Result of SubTask1: " << result1 << std::endl;
    std::cout << "Result of SubTask2: " << result2 << std::endl;
    std::cout << "Result of SubTask3: " << result3 << std::endl;
    std::cout << "Result of SubTask4: " << result4 << std::endl;
    std::cout << "Result of SubTask5: " << result5 << std::endl;
    std::cout << "Result of SubTask6: " << result6 << std::endl;
    std::cout << "Result of SubTask7: " << result7 << std::endl;
    std::cout << "Result of SubTask8: " << result8 << std::endl;
    std::cout << "Result of SubTask9: " << result9 << std::endl;
    std::cout << "Result of SubTask10: " << result10 << std::endl;
    return 0;
}
