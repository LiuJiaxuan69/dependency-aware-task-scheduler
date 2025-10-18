# 最小 CMake 版本要求
cmake_minimum_required(VERSION 3.18)

# 项目信息
project(dependency-aware-task-scheduler VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 子目录：示例、性能、功能测试
add_subdirectory(samples)
add_subdirectory(benchmarks)
add_subdirectory(functional_tests)