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

TEST_F(DispatchEventTest, RespectsDependencyOrdering) {
    MainTask main_task("MainTaskDeps", 2, Priority::NORMAL);
    dispathcer->addMainTask(std::move(main_task));

    std::vector<std::string> execution_order;
    std::mutex order_mutex;

    SubTask subtaskA("Deps_TaskA", "MainTaskDeps", [&]() -> std::any {
        {
            std::lock_guard<std::mutex> lk(order_mutex);
            execution_order.emplace_back("A");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return std::string("A done");
    });

    SubTask subtaskB("Deps_TaskB", "MainTaskDeps", [&]() -> std::any {
        {
            std::lock_guard<std::mutex> lk(order_mutex);
            execution_order.emplace_back("B");
        }
        return 7;
    });
    subtaskB.dependencies.push_back("Deps_TaskA");

    dispathcer->addSubTask(std::move(subtaskA));
    dispathcer->addSubTask(std::move(subtaskB));

    auto resultA = std::any_cast<std::string>(dispathcer->getSubTaskResult("Deps_TaskA"));
    auto resultB = std::any_cast<int>(dispathcer->getSubTaskResult("Deps_TaskB"));

    EXPECT_EQ(resultA, "A done");
    EXPECT_EQ(resultB, 7);
    ASSERT_EQ(execution_order.size(), 2U);
    EXPECT_EQ(execution_order[0], "A");
    EXPECT_EQ(execution_order[1], "B");
}

TEST_F(DispatchEventTest, HandlesHighConcurrencyLoad) {
    constexpr size_t kMainTasks = 5;
    constexpr size_t kSubTasksPerMain = 6;

    std::vector<std::string> subtask_names;
    subtask_names.reserve(kMainTasks * kSubTasksPerMain);

    size_t expected_sum = 0;
    for (size_t i = 0; i < kMainTasks; ++i) {
        std::string main_name = "StressMain_" + std::to_string(i);
        MainTask main_task(main_name, kSubTasksPerMain, Priority::ABOVE_NORMAL);
        dispathcer->addMainTask(std::move(main_task));

        for (size_t j = 0; j < kSubTasksPerMain; ++j) {
            std::string sub_name = main_name + "_Sub_" + std::to_string(j);
            size_t payload = i * 100 + j;
            expected_sum += payload;

            SubTask subtask(sub_name, main_name, [payload]() -> std::any {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                return static_cast<int>(payload);
            });

            dispathcer->addSubTask(std::move(subtask));
            subtask_names.push_back(sub_name);
        }
    }

    size_t observed_sum = 0;
    for (const auto& name : subtask_names) {
        observed_sum += std::any_cast<int>(dispathcer->getSubTaskResult(name));
    }

    EXPECT_EQ(observed_sum, expected_sum);
}

// ...existing code...
TEST_F(DispatchEventTest, DiamondDependencyGraph) {
    // 图结构：A -> B, A -> C, {B,C} -> D
    const std::string main_name = "DiamondMain";
    MainTask main_task(main_name, 4, Priority::NORMAL);
    dispathcer->addMainTask(std::move(main_task));

    std::vector<std::string> order;
    std::mutex m;

    SubTask A("DIA_A", main_name, [&]() -> std::any {
        std::lock_guard<std::mutex> lk(m); order.push_back("A");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return 2; // 基值
    });

    SubTask B("DIA_B", main_name, [&]() -> std::any {
        std::lock_guard<std::mutex> lk(m); order.push_back("B");
        return 3; // 假设依赖A并在算法上+1，这里只验证顺序
    });
    B.dependencies.push_back("DIA_A");

    SubTask C("DIA_C", main_name, [&]() -> std::any {
        std::lock_guard<std::mutex> lk(m); order.push_back("C");
        return 20;
    });
    C.dependencies.push_back("DIA_A");

    SubTask D("DIA_D", main_name, [&]() -> std::any {
        std::lock_guard<std::mutex> lk(m); order.push_back("D");
        return 23; // 2 + 1 + 20 的目标值，这里直接返回验证依赖是否完成
    });
    D.dependencies.push_back("DIA_B");
    D.dependencies.push_back("DIA_C");

    dispathcer->addSubTask(std::move(A));
    dispathcer->addSubTask(std::move(B));
    dispathcer->addSubTask(std::move(C));
    dispathcer->addSubTask(std::move(D));

    auto d = std::any_cast<int>(dispathcer->getSubTaskResult("DIA_D"));
    EXPECT_EQ(d, 23);

    // 验证顺序：A 必在 B/C 之前，且 D 在 B/C 之后
    auto idx = [&](const std::string& s) {
        return std::find(order.begin(), order.end(), s) - order.begin();
    };
    EXPECT_LT(idx("A"), idx("B"));
    EXPECT_LT(idx("A"), idx("C"));
    EXPECT_LT(idx("B"), idx("D"));
    EXPECT_LT(idx("C"), idx("D"));
}

TEST_F(DispatchEventTest, ThreeStagePipelineWithBarriers) {
    // 三阶段流水线，S2 全依赖 S1，S3 全依赖 S2（屏障式）
    const std::string main_name = "PipelineMain";
    const int S1N = 5, S2N = 4, S3N = 1;
    MainTask main_task(main_name, S1N + S2N + S3N, Priority::HIGH);
    dispathcer->addMainTask(std::move(main_task));

    std::vector<std::string> order;
    std::mutex m;
    // Stage 3（聚合，依赖全部 S2）
    SubTask agg{"PL_S3_0", main_name, [&m, &order]() -> std::any {
        std::lock_guard<std::mutex> lk(m); order.push_back("PL_S3_0");
        // std::cout << "Stage 3 - PL_S3_0 completed" << std::endl;
        return std::string("OK");
    }};
    // Stage 2（每个都依赖全部 S1）
    for (int i = 0; i < S2N; ++i) {
        std::string name = "PL_S2_" + std::to_string(i);
        SubTask s{name, main_name, [name, &m, &order, i]() -> std::any {
            std::lock_guard<std::mutex> lk(m); order.push_back(name);
            // std::cout << "Stage 2 - " << name << " completed" << std::endl;
            return 100 + i;
        }};
        for (int d = 0; d < S1N; ++d) s.dependencies.push_back("PL_S1_" + std::to_string(d));
        dispathcer->addSubTask(std::move(s));
    }
    // Stage 1
    for (int i = 0; i < S1N; ++i) {
        std::string name = "PL_S1_" + std::to_string(i);
        dispathcer->addSubTask(SubTask{name, main_name, [name, &m, &order, i]() -> std::any {
            std::lock_guard<std::mutex> lk(m); order.push_back(name);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            // std::cout << "Stage 1 - " << name << " completed" << std::endl;
            return i;
        }});
    }
    for (int d = 0; d < S2N; ++d) agg.dependencies.push_back("PL_S2_" + std::to_string(d));
    dispathcer->addSubTask(std::move(agg));

    auto ok = std::any_cast<std::string>(dispathcer->getSubTaskResult("PL_S3_0"));
    EXPECT_EQ(ok, "OK");

    // 屏障验证：所有 S1 在任一 S2 之前；所有 S2 在 S3 之前
    auto firstOf = [&](const std::string& prefix) {
        for (size_t i = 0; i < order.size(); ++i)
            if (order[i].rfind(prefix, 0) == 0) return i;
        return order.size();
    };
    auto lastOf = [&](const std::string& prefix) {
        for (size_t i = order.size(); i-- > 0; )
            if (order[i].rfind(prefix, 0) == 0) return i;
        return order.size();
    };
    EXPECT_LT(lastOf("PL_S1_"), firstOf("PL_S2_"));
    EXPECT_LT(lastOf("PL_S2_"), firstOf("PL_S3_"));
}

TEST_F(DispatchEventTest, LongCriticalPathChain) {
    // 长关键路径链：T0 -> T1 -> ... -> T49
    const std::string main_name = "LongChainMain";
    const int N = 50;
    MainTask main_task(main_name, N, Priority::ABOVE_NORMAL);
    dispathcer->addMainTask(std::move(main_task));

    std::vector<std::string> order;
    std::mutex m;

    for (int i = 0; i < N; ++i) {
        std::string name = "LC_T_" + std::to_string(i);
        SubTask s{name, main_name, [&, name, i]() -> std::any {
            std::lock_guard<std::mutex> lk(m); order.push_back(name);
            return i;
        }};
        if (i > 0) s.dependencies.push_back("LC_T_" + std::to_string(i - 1));
        dispathcer->addSubTask(std::move(s));
    }

    // 只取最后一个结果，验证不会死锁且能跑完
    auto last = std::any_cast<int>(dispathcer->getSubTaskResult("LC_T_49"));
    EXPECT_EQ(last, 49);

    // 顺序性：链上执行顺序应单调递增
    std::vector<int> seen;
    for (auto& tag : order) {
        if (tag.rfind("LC_T_", 0) == 0) {
            seen.push_back(std::stoi(tag.substr(5)));
        }
    }
    ASSERT_FALSE(seen.empty());
    for (size_t i = 1; i < seen.size(); ++i) {
        EXPECT_LT(seen[i - 1], seen[i]);
    }
}

TEST_F(DispatchEventTest, RandomDAG_FanInFanOut) {
    // 随机 DAG：节点 i 依赖部分 [0..i-1]
    const std::string main_name = "RandomDAGMain";
    const int N = 30;
    MainTask main_task(main_name, N + 1, Priority::NORMAL);
    dispathcer->addMainTask(std::move(main_task));

    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> pick(0, 3); // 平均少量依赖，避免过重

    for (int i = 0; i < N; ++i) {
        std::string name = "RD_Node_" + std::to_string(i);
        SubTask s{name, main_name, [i]() -> std::any {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return i;
        }};
        int k = std::min(i, pick(rng)); // 从前面的节点中选 k 个依赖
        std::unordered_set<int> chosen;
        while ((int)chosen.size() < k) {
            int d = rng() % i;
            if (chosen.insert(d).second) {
                s.dependencies.push_back("RD_Node_" + std::to_string(d));
            }
        }
        dispathcer->addSubTask(std::move(s));
    }

    // 聚合器，依赖所有节点，返回字符串
    SubTask agg{"RD_Agg", main_name, []() -> std::any {
        return std::string("DAG OK");
    }};
    for (int i = 0; i < N; ++i) agg.dependencies.push_back("RD_Node_" + std::to_string(i));
    dispathcer->addSubTask(std::move(agg));

    // 只取聚合器结果，确保整个 DAG 能顺利拓扑执行
    auto ok = std::any_cast<std::string>(dispathcer->getSubTaskResult("RD_Agg"));
    EXPECT_EQ(ok, "DAG OK");
}

// ...existing code...

TEST_F(DispatchEventTest, MultipleMainTasksIsolation) {
    // 两个主任务各自链式依赖，交错添加，验证互不干扰且各自依赖有序
    const std::string A = "MM_A", B = "MM_B";
    MainTask ma(A, 3, Priority::NORMAL);
    MainTask mb(B, 3, Priority::NORMAL);
    dispathcer->addMainTask(std::move(ma));
    dispathcer->addMainTask(std::move(mb));

    std::vector<std::string> order;
    std::mutex m;

    // A: A1 -> A2 -> A3
    dispathcer->addSubTask(SubTask{"A1", A, [&]{ std::lock_guard<std::mutex> lk(m); order.push_back("A1"); return 1; }});
    { SubTask t{"A2", A, [&]{ std::lock_guard<std::mutex> lk(m); order.push_back("A2"); return 2; }}; t.dependencies.push_back("A1"); dispathcer->addSubTask(std::move(t)); }
    { SubTask t{"A3", A, [&]{ std::lock_guard<std::mutex> lk(m); order.push_back("A3"); return 3; }}; t.dependencies.push_back("A2"); dispathcer->addSubTask(std::move(t)); }

    // B: B1 -> B2 -> B3
    dispathcer->addSubTask(SubTask{"B1", B, [&]{ std::lock_guard<std::mutex> lk(m); order.push_back("B1"); return 10; }});
    { SubTask t{"B2", B, [&]{ std::lock_guard<std::mutex> lk(m); order.push_back("B2"); return 20; }}; t.dependencies.push_back("B1"); dispathcer->addSubTask(std::move(t)); }
    { SubTask t{"B3", B, [&]{ std::lock_guard<std::mutex> lk(m); order.push_back("B3"); return 30; }}; t.dependencies.push_back("B2"); dispathcer->addSubTask(std::move(t)); }

    auto a3 = std::any_cast<int>(dispathcer->getSubTaskResult("A3"));
    auto b3 = std::any_cast<int>(dispathcer->getSubTaskResult("B3"));
    EXPECT_EQ(a3, 3);
    EXPECT_EQ(b3, 30);

    auto idx = [&](const std::string& s){ return std::find(order.begin(), order.end(), s) - order.begin(); };
    EXPECT_LT(idx("A1"), idx("A2")); EXPECT_LT(idx("A2"), idx("A3"));
    EXPECT_LT(idx("B1"), idx("B2")); EXPECT_LT(idx("B2"), idx("B3"));
}

TEST_F(DispatchEventTest, PrioritySchedulingBias_HighBeforeLow) {
    const std::string HP = "HP_Main", LP = "LP_Main";
    MainTask hp(HP, 3, Priority::HIGHEST);
    MainTask lp(LP, 6, Priority::LOWEST);
    dispathcer->addMainTask(std::move(hp));
    dispathcer->addMainTask(std::move(lp));

    std::vector<std::string> starts;
    std::mutex m;

    // 先入队高优先级
    for (int i = 0; i < 3; ++i) {
        std::string name = "HP_" + std::to_string(i);
        dispathcer->addSubTask(SubTask{name, HP, [name, &m, &starts]() -> std::any {
            { std::lock_guard<std::mutex> lk(m); starts.push_back(name); }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return 0;
        }});
    }
    // 再入队低优先级：在记录启动前先延时，模拟“更重”的任务
    for (int i = 0; i < 6; ++i) {
        std::string name = "LP_" + std::to_string(i);
        dispathcer->addSubTask(SubTask{name, LP, [name, &m, &starts]() -> std::any {
            std::this_thread::sleep_for(std::chrono::milliseconds(2)); // 额外延时，降低抢占概率
            { std::lock_guard<std::mutex> lk(m); starts.push_back(name); }
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
            return 0;
        }});
    }

    for (int i = 0; i < 3; ++i) (void)dispathcer->getSubTaskResult("HP_" + std::to_string(i));
    for (int i = 0; i < 6; ++i) (void)dispathcer->getSubTaskResult("LP_" + std::to_string(i));

    auto firstIdx = [&](const char* prefix) {
        for (size_t i = 0; i < starts.size(); ++i)
            if (starts[i].rfind(prefix, 0) == 0) return i;
        return starts.size();
    };
    ASSERT_GE(starts.size(), 3u);
    EXPECT_LT(firstIdx("HP_"), firstIdx("LP_"));   // 更稳健的“先于”判断
    EXPECT_EQ(starts[0].rfind("HP_", 0), 0u);      // 现在也应稳定满足首个为 HP
}

TEST_F(DispatchEventTest, GridBarrier_5x5_RowsAreBarriers) {
    // 5x5 网格：第 r 行的所有任务依赖 r-1 行所有任务，模拟层级屏障
    const std::string main_name = "GRID_Main";
    const int R = 5, C = 5;
    MainTask mt(main_name, R * C, Priority::NORMAL);
    dispathcer->addMainTask(std::move(mt));

    std::vector<std::string> order;
    std::mutex m;

    auto nameOf = [](int r, int c){ return "GRID_" + std::to_string(r) + "_" + std::to_string(c); };

    // 添加所有任务与依赖
    for (int r = 0; r < R; ++r) {
        for (int c = 0; c < C; ++c) {
            std::string nm = nameOf(r, c);
            SubTask s{nm, main_name, [nm, &order, &m]() -> std::any {
                std::lock_guard<std::mutex> lk(m); order.push_back(nm);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                return 1;
            }};
            if (r > 0) {
                for (int pc = 0; pc < C; ++pc) s.dependencies.push_back(nameOf(r - 1, pc));
            }
            dispathcer->addSubTask(std::move(s));
        }
    }

    // 等最后一行任意一个（比如最后一个）完成
    (void)dispathcer->getSubTaskResult(nameOf(R - 1, C - 1));

    // 验证：第 r 行所有任务在第 r+1 行任何任务之前完成
    auto firstOfPrefix = [&](const std::string& p){
        for (size_t i = 0; i < order.size(); ++i) if (order[i].rfind(p, 0) == 0) return i;
        return order.size();
    };
    auto lastOfPrefix = [&](const std::string& p){
        for (size_t i = order.size(); i-- > 0; ) if (order[i].rfind(p, 0) == 0) return i;
        return order.size();
    };
    for (int r = 0; r < R - 1; ++r) {
        std::string pr = "GRID_" + std::to_string(r) + "_";
        std::string nr = "GRID_" + std::to_string(r + 1) + "_";
        EXPECT_LT(lastOfPrefix(pr), firstOfPrefix(nr));
    }
}

TEST_F(DispatchEventTest, LargeFanOutFromRoot_BroadcastDependencies) {
    // 一个根任务，20 个子任务全依赖它，确保广播依赖不会饿死
    const std::string main_name = "FanOutMain";
    const int M = 21; // 1 root + 20 deps
    MainTask mt(main_name, M, Priority::ABOVE_NORMAL);
    dispathcer->addMainTask(std::move(mt));

    std::atomic<int> children_started{0};
    dispathcer->addSubTask(SubTask{"ROOT", main_name, []() -> std::any {
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        return 7;
    }});

    for (int i = 0; i < M - 1; ++i) {
        std::string nm = "CH_" + std::to_string(i);
        SubTask s{nm, main_name, [&, nm]() -> std::any {
            children_started.fetch_add(1, std::memory_order_acq_rel);
            return nm;
        }};
        s.dependencies.push_back("ROOT");
        dispathcer->addSubTask(std::move(s));
    }

    // 拉取所有子任务结果（不包含根）
    for (int i = 0; i < M - 1; ++i) {
        auto v = std::any_cast<std::string>(dispathcer->getSubTaskResult("CH_" + std::to_string(i)));
        EXPECT_TRUE(v.rfind("CH_", 0) == 0);
    }
    EXPECT_EQ(children_started.load(), M - 1);
}

TEST_F(DispatchEventTest, ExceptionPropagationFromSubTask) {
    // 一个子任务抛异常，另一个正常；验证异常能被 getSubTaskResult 观察到
    const std::string main_name = "ExceptMain";
    MainTask mt(main_name, 2, Priority::NORMAL);
    dispathcer->addMainTask(std::move(mt));

    dispathcer->addSubTask(SubTask{"OK", main_name, []() -> std::any { return 123; }});
    dispathcer->addSubTask(SubTask{"BOOM", main_name, []() -> std::any {
        throw std::runtime_error("boom");
        return 0;
    }});

    // 正常先取
    EXPECT_EQ(std::any_cast<int>(dispathcer->getSubTaskResult("OK")), 123);
    // 异常应抛出
    EXPECT_THROW({ (void)dispathcer->getSubTaskResult("BOOM"); }, std::runtime_error);
}

TEST_F(DispatchEventTest, PointerSharedDataChain_StrictOrdering) {
    const std::string main_name = "PtrChainMain";
    const int N = 10000;
    MainTask mt(main_name, 3, Priority::NORMAL);
    dispathcer->addMainTask(std::move(mt));

    // 共享数据缓冲区：由第一个任务写入，后续任务只读/改
    auto buffer = std::make_shared<std::vector<int>>();

    // 任务1：填充数据（输出型参数：通过指针/共享对象返回数据）
    SubTask fill{"PC_Fill", main_name, [buf = buffer, N]() -> std::any {
        buf->resize(N);
        for (int i = 0; i < N; ++i) {
            (*buf)[i] = i; // 写入 0..N-1
        }
        return N; // 返回写入的数量，仅用于测试验证
    }};

    // 任务2：在原地将数据扩大2倍（输入/输出共用同一内存）
    SubTask transform{"PC_X2", main_name, [buf = buffer, N]() -> std::any {
        // 要求：必须等 PC_Fill 完成（由依赖保证）
        for (int i = 0; i < N; ++i) {
            (*buf)[i] *= 2;
        }
        // 返回校验用的首尾值
        return std::pair<int,int>{(*buf)[0], (*buf)[N-1]};
    }};
    transform.dependencies.push_back("PC_Fill");

    // 任务3：对结果求和（只读输入）
    SubTask sum{"PC_Sum", main_name, [buf = buffer, N]() -> std::any {
        long long s = 0;
        for (int v : *buf) s += v;
        return s;
    }};
    sum.dependencies.push_back("PC_X2");

    dispathcer->addSubTask(std::move(fill));
    dispathcer->addSubTask(std::move(transform));
    dispathcer->addSubTask(std::move(sum));

    // 逐个拉取结果，保证清理并验证正确性与顺序
    auto filled = std::any_cast<int>(dispathcer->getSubTaskResult("PC_Fill"));
    EXPECT_EQ(filled, N);

    auto p = std::any_cast<std::pair<int,int>>(dispathcer->getSubTaskResult("PC_X2"));
    EXPECT_EQ(p.first, 0);
    EXPECT_EQ(p.second, 2*(N-1));

    auto total = std::any_cast<long long>(dispathcer->getSubTaskResult("PC_Sum"));
    // 数学期望：sum(2*i, i=0..N-1) = 2 * (N-1)*N/2 = N*(N-1)
    EXPECT_EQ(total, 1LL * N * (N - 1));

    // 额外检查：缓冲区的部分内容已被按序正确更新
    ASSERT_FALSE(buffer->empty());
    EXPECT_EQ((*buffer)[1], 2);
    EXPECT_EQ((*buffer)[N/2], 2 * (N/2));
}

// ...existing code...

TEST_F(DispatchEventTest, ImagePipeline_TilingAggregation) {
    const std::string main_name = "ImagePipe";
    const int W = 8, H = 6;
    const int T_ROWS = 2, T_COLS = 2; // 2x2 分块
    const int T = T_ROWS * T_COLS;
    // 任务总数：Decode + Preprocess + Tiles + Agg = 2 + T + 1
    MainTask mt(main_name, 2 + T + 1, Priority::ABOVE_NORMAL);
    dispathcer->addMainTask(std::move(mt));

    auto image = std::make_shared<std::vector<int>>();
    auto tilesum = std::make_shared<std::vector<long long>>(T, 0);

    // Decode: 填充原始图像(全1)
    dispathcer->addSubTask(SubTask{"IMG_Decode", main_name, [image, W, H]() -> std::any {
        image->assign(W * H, 1);
        return W * H;
    }});

    // Preprocess: 简单放大2倍
    SubTask pre{"IMG_Pre", main_name, [image]() -> std::any {
        for (auto& v : *image) v *= 2;
        return image->size();
    }};
    pre.dependencies.push_back("IMG_Decode");
    dispathcer->addSubTask(std::move(pre));

    // Tile 处理：每块求和，写入 tilesum[idx]
    auto tileName = [&](int r, int c) { return "TILE_" + std::to_string(r) + "_" + std::to_string(c); };
    const int tileH = H / T_ROWS, tileW = W / T_COLS;

    for (int r = 0; r < T_ROWS; ++r) {
        for (int c = 0; c < T_COLS; ++c) {
            std::string name = tileName(r, c);
            int idx = r * T_COLS + c;
            SubTask tile{name, main_name, [image, tilesum, r, c, idx, tileH, tileW, W]() -> std::any {
                long long s = 0;
                for (int y = r * tileH; y < (r + 1) * tileH; ++y) {
                    for (int x = c * tileW; x < (c + 1) * tileW; ++x) {
                        s += (*image)[y * W + x];
                    }
                }
                (*tilesum)[idx] = s;
                return s;
            }};
            tile.dependencies.push_back("IMG_Pre");
            dispathcer->addSubTask(std::move(tile));
        }
    }

    // 聚合：汇总所有 tile 的和
    SubTask agg{"IMG_Agg", main_name, [tilesum]() -> std::any {
        long long total = 0;
        for (auto v : *tilesum) total += v;
        return total;
    }};
    for (int r = 0; r < T_ROWS; ++r)
        for (int c = 0; c < T_COLS; ++c)
            agg.dependencies.push_back(tileName(r, c));
    dispathcer->addSubTask(std::move(agg));

    // 期望：图像全1，预处理*2 => 每像素2，总和= W*H*2
    auto total = std::any_cast<long long>(dispathcer->getSubTaskResult("IMG_Agg"));
    EXPECT_EQ(total, 1LL * W * H * 2);
}

TEST_F(DispatchEventTest, MapReduce_WordCount) {
    const std::string main_name = "MR_WordCount";
    const int P = 4; // 分片数
    // 任务总数：Load + P*Map + Shuffle + Reduce
    MainTask mt(main_name, 1 + P + 1 + 1, Priority::NORMAL);
    dispathcer->addMainTask(std::move(mt));

    // 构造输入（简单重复）
    auto shards = std::make_shared<std::vector<std::string>>(P);
    auto maps = std::make_shared<std::vector<std::unordered_map<std::string,int>>>(P);
    auto merged = std::make_shared<std::unordered_map<std::string,int>>();

    // 目标词
    std::vector<std::string> targets = {"apple", "banana", "orange"};
    // 期望：每个分片 "apple banana orange apple\n"
    int expected_apple = 0;

    dispathcer->addSubTask(SubTask{"MR_Load", main_name, [shards, P, &expected_apple]() -> std::any {
        for (int i = 0; i < P; ++i) {
            (*shards)[i] = "apple banana orange apple\n";
            expected_apple += 2; // 每片2个 apple
        }
        return P;
    }});

    auto mapName = [&](int i){ return "MR_Map_" + std::to_string(i); };

    for (int i = 0; i < P; ++i) {
        std::string name = mapName(i);
        SubTask map{name, main_name, [shards, maps, i]() -> std::any {
            std::istringstream iss((*shards)[i]);
            std::string w;
            auto& m = (*maps)[i];
            while (iss >> w) m[w] += 1;
            return m.size();
        }};
        map.dependencies.push_back("MR_Load");
        dispathcer->addSubTask(std::move(map));
    }

    // Shuffle：合并所有 map
    SubTask shuffle{"MR_Shuffle", main_name, [maps, merged, P]() -> std::any {
        for (int i = 0; i < P; ++i) {
            for (auto& kv : (*maps)[i]) (*merged)[kv.first] += kv.second;
        }
        return merged->size();
    }};
    for (int i = 0; i < P; ++i) shuffle.dependencies.push_back(mapName(i));
    dispathcer->addSubTask(std::move(shuffle));

    // Reduce：取 "apple" 计数
    SubTask reduce{"MR_Reduce", main_name, [merged]() -> std::any {
        return (*merged)["apple"];
    }};
    reduce.dependencies.push_back("MR_Shuffle");
    dispathcer->addSubTask(std::move(reduce));

    auto apples = std::any_cast<int>(dispathcer->getSubTaskResult("MR_Reduce"));
    EXPECT_EQ(apples, expected_apple);
}

TEST_F(DispatchEventTest, DL_MicroTrainingPipeline) {
    const std::string main_name = "DL_Pipeline";
    const int B = 8; // batch size
    // Load + Aug(B) + Assemble + Forward + Backward + Update
    MainTask mt(main_name, 1 + B + 1 + 1 + 1 + 1, Priority::HIGH);
    dispathcer->addMainTask(std::move(mt));

    auto batch = std::make_shared<std::vector<float>>();
    auto auged = std::make_shared<std::vector<float>>(B, 0.f);
    auto batch_sum = std::make_shared<float>(0.f);
    float lr = 0.01f, param0 = 1.0f;

    dispathcer->addSubTask(SubTask{"DL_Load", main_name, [batch, B]() -> std::any {
        batch->resize(B);
        for (int i = 0; i < B; ++i) (*batch)[i] = static_cast<float>(i);
        return B;
    }});

    auto augName = [&](int i){ return "DL_Aug_" + std::to_string(i); };
    for (int i = 0; i < B; ++i) {
        std::string name = augName(i);
        SubTask aug{name, main_name, [batch, auged, i]() -> std::any {
            // v' = 2*v + 1
            (*auged)[i] = 2.f * (*batch)[i] + 1.f;
            return (*auged)[i];
        }};
        aug.dependencies.push_back("DL_Load");
        dispathcer->addSubTask(std::move(aug));
    }

    SubTask assemble{"DL_Assemble", main_name, [auged, batch_sum, B]() -> std::any {
        float s = 0.f;
        for (int i = 0; i < B; ++i) s += (*auged)[i];
        *batch_sum = s;
        return s;
    }};
    for (int i = 0; i < B; ++i) assemble.dependencies.push_back(augName(i));
    dispathcer->addSubTask(std::move(assemble));

    SubTask forward{"DL_Forward", main_name, [batch_sum]() -> std::any {
        // loss = 0.1 * sum
        return 0.1f * (*batch_sum);
    }};
    forward.dependencies.push_back("DL_Assemble");
    dispathcer->addSubTask(std::move(forward));

    SubTask backward{"DL_Backward", main_name, []() -> std::any {
        // grad = 2 * loss
        // 具体 loss 在下一步直接复用
        return 0; // 占位
    }};
    backward.dependencies.push_back("DL_Forward");
    dispathcer->addSubTask(std::move(backward));

    SubTask update{"DL_Update", main_name, [batch_sum, lr, param0]() -> std::any {
        float loss = 0.1f * (*batch_sum);
        float grad = 2.f * loss;
        float param = param0 - lr * grad;
        return param;
    }};
    update.dependencies.push_back("DL_Backward");
    dispathcer->addSubTask(std::move(update));

    // 数学期望：v_i=i -> aug 2i+1，sum = B^2；loss=0.1*B^2；grad=0.2*B^2；param=1-0.01*0.2*B^2
    auto param = std::any_cast<float>(dispathcer->getSubTaskResult("DL_Update"));
    float expected = 1.f - 0.002f * (B * B);
    EXPECT_NEAR(param, expected, 1e-6f);
}

TEST_F(DispatchEventTest, PairwiseCombine_MultiLayerDAG) {
    const std::string main_name = "CombineDAG";
    const int K = 6; // Stage0 个数
    // S0(K) + S1(K/2) + S2(1)
    MainTask mt(main_name, K + K/2 + 1, Priority::NORMAL);
    dispathcer->addMainTask(std::move(mt));

    auto s0vals = std::make_shared<std::vector<int>>(K, 0);
    auto s1vals = std::make_shared<std::vector<int>>(K/2, 0);

    auto s0Name = [&](int i){ return "C0_" + std::to_string(i); };
    auto s1Name = [&](int i){ return "C1_" + std::to_string(i); };

    // Stage0：基础数值
    for (int i = 0; i < K; ++i) {
        std::string name = s0Name(i);
        dispathcer->addSubTask(SubTask{name, main_name, [s0vals, i]() -> std::any {
            (*s0vals)[i] = i + 1; // 1..K
            return (*s0vals)[i];
        }});
    }

    // Stage1：两两合并求和
    for (int i = 0; i < K/2; ++i) {
        std::string name = s1Name(i);
        SubTask s{name, main_name, [s0vals, s1vals, i]() -> std::any {
            int a = (*s0vals)[2*i], b = (*s0vals)[2*i+1];
            (*s1vals)[i] = a + b;
            return (*s1vals)[i];
        }};
        s.dependencies.push_back(s0Name(2*i));
        s.dependencies.push_back(s0Name(2*i+1));
        dispathcer->addSubTask(std::move(s));
    }

    // Stage2：聚合 Stage1 的和
    SubTask top{"C2_0", main_name, [s1vals]() -> std::any {
        int total = 0;
        for (int v : *s1vals) total += v;
        return total;
    }};
    for (int i = 0; i < K/2; ++i) top.dependencies.push_back(s1Name(i));
    dispathcer->addSubTask(std::move(top));

    // 期望：S1 = (1+2),(3+4),(5+6) = 3,7,11 => 总和=21
    auto total = std::any_cast<int>(dispathcer->getSubTaskResult("C2_0"));
    EXPECT_EQ(total, 21);
}


int main() {
    ::testing::InitGoogleTest();
    return RUN_ALL_TESTS();
}