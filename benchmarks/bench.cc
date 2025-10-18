#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <cstdlib>
#include <numeric>
#include "EventDispatcher.hpp"
#include "Task.hpp"

using Clock = std::chrono::high_resolution_clock;
using ms = std::chrono::milliseconds;

static long long cpu_work(int iters) {
    // 简单的 CPU 计算，避免被优化掉
    volatile long long acc = 0;
    for (int i = 0; i < iters; ++i) {
        acc += i % 7;
    }
    return acc;
}

struct BenchConfig {
    int mains = 50;          // 主任务数量
    int subs = 100;          // 每个主任务的子任务数量
    int work = 20000;        // 每个子任务的 CPU 迭代数
    std::string pattern = "chain"; // chain | indep | barrier
};

static BenchConfig parse_args(int argc, char** argv) {
    BenchConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--mains") cfg.mains = std::atoi(next());
        else if (a == "--subs") cfg.subs = std::atoi(next());
        else if (a == "--work") cfg.work = std::atoi(next());
        else if (a == "--pattern") cfg.pattern = next();
    }
    return cfg;
}

int main(int argc, char** argv) {
    BenchConfig cfg = parse_args(argc, argv);

    auto& d = EventDispatcher<>::getInstance();
    d.start();

    const auto t0 = Clock::now();

    // 提交主任务
    for (int m = 0; m < cfg.mains; ++m) {
        std::string main_name = "Main_" + std::to_string(m);
        d.addMainTask(MainTask(main_name, static_cast<size_t>(cfg.subs), Priority::NORMAL));
    }

    // 构造并提交子任务
    std::vector<std::vector<std::string>> sub_names(cfg.mains);
    for (int m = 0; m < cfg.mains; ++m) {
        sub_names[m].reserve(cfg.subs);
        for (int s = 0; s < cfg.subs; ++s) {
            sub_names[m].push_back("M" + std::to_string(m) + "_S" + std::to_string(s));
        }
    }

    for (int m = 0; m < cfg.mains; ++m) {
        const std::string main_name = "Main_" + std::to_string(m);
        for (int s = 0; s < cfg.subs; ++s) {
            SubTask st{sub_names[m][s], main_name, [w = cfg.work]() -> std::any {
                long long v = cpu_work(w);
                return static_cast<int>(v & 0x7fffffff);
            }};

            if (cfg.pattern == "chain") {
                if (s > 0) st.dependencies.push_back(sub_names[m][s - 1]);
            } else if (cfg.pattern == "indep") {
                // 无依赖
            } else if (cfg.pattern == "barrier") {
                // 三阶段屏障：0..A-1 | A..B-1 依赖全部 stage1 | B..S-1 依赖全部 stage2
                int A = cfg.subs / 3;
                int B = (cfg.subs * 2) / 3;
                if (s >= A && s < B) {
                    for (int k = 0; k < A; ++k) st.dependencies.push_back(sub_names[m][k]);
                } else if (s >= B) {
                    for (int k = A; k < B; ++k) st.dependencies.push_back(sub_names[m][k]);
                }
            }

            d.addSubTask(std::move(st));
        }
    }

    const auto t_submit_done = Clock::now();

    // 获取全部结果，确保清理映射并等待完成
    long long checksum = 0;
    for (int m = 0; m < cfg.mains; ++m) {
        for (int s = 0; s < cfg.subs; ++s) {
            try {
                int v = d.getSubTaskResultAs<int>(sub_names[m][s]);
                checksum += v;
            } catch (const std::exception& e) {
                std::cerr << "Error getting result for " << sub_names[m][s]
                          << ": " << e.what() << std::endl;
                return 2;
            }
        }
    }

    const auto t_done = Clock::now();
    d.Stop();

    const auto submit_ms = std::chrono::duration_cast<ms>(t_submit_done - t0).count();
    const auto total_ms  = std::chrono::duration_cast<ms>(t_done - t0).count();
    const long long total_tasks = 1LL * cfg.mains * cfg.subs;
    const double throughput = total_tasks / (total_ms / 1000.0);

    std::cout << "pattern=" << cfg.pattern
              << " mains=" << cfg.mains
              << " subsPerMain=" << cfg.subs
              << " workIters=" << cfg.work
              << " | submit_ms=" << submit_ms
              << " total_ms=" << total_ms
              << " | tasks=" << total_tasks
              << " throughput=" << throughput << " tasks/s"
              << " | checksum=" << checksum
              << std::endl;

    return 0;
}
