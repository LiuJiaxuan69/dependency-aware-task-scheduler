# 最小 CMake 版本要求
cmake_minimum_required(VERSION 3.18)

# 项目信息
project(exper2 VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 可执行文件
add_executable(tests test.cc)
add_executable(main main.cc)

# 添加 gtest 依赖
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})
target_link_libraries(tests ${GTEST_LIBRARIES} pthread)
target_link_libraries(main pthread)

# 添加 spdlog 依赖
find_package(spdlog REQUIRED)
target_link_libraries(tests spdlog::spdlog)

# 添加测试
enable_testing()
add_test(NAME all_test COMMAND tests)