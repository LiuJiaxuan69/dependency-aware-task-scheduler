# OS 实验项目：多线程任务调度器（依赖感知 + 线程池 + 信号量/条件变量）

本项目实现了一个支持任务依赖的多线程任务调度器，包含：
- 事件调度器（EventDispatcher）：支持主任务/子任务、任务依赖（DAG）、优先级队列
- 线程池（ThreadPool）：固定工作线程+无锁语义的环形队列（RingQueue）
- 自研同步原语：基于 POSIX 信号量的 Mutex/ConditionVariable 封装（含 PV 操作）
- 结果传递：子任务返回值统一包装为 `std::any`，调用方通过阻塞接口获取
- 异常透传：子任务异常以 `std::exception_ptr` 回传，调用方在 `getSubTaskResult` 处重抛

该项目满足并超出“多线程+有意义的同步问题+使用 OS 底层信号量和 PV 操作”的实验要求。

---

## 目录结构

```
/os_exper
├─ exper1/                  # 其他实验
├─ exper2/                  # 任务调度器主体工程
│  ├─ CMakeLists.txt
│  ├─ EventDispatcher.hpp   # 调度器（模板，包含事件循环与依赖处理）
│  ├─ Task.hpp              # MainTask/SubTask/CompletionEvent/SubTaskResult 等
│  ├─ ThreadPool.hpp        # 线程池（支持返回 future）、异常兜底
│  ├─ RingQueue.hpp         # 环形任务队列，基于信号量实现生产/消费
│  ├─ Mutex.hpp / LockGuard.hpp / UniqueLock.hpp
│  ├─ ConditionVariable.hpp # 基于信号量实现的条件变量（支持 notify_all）
│  ├─ thread.hpp            # 对 pthread 的轻量封装
│  ├─ main.cc               # 示例程序
│  ├─ test.cc               # GTest 用例（复杂依赖/屏障/随机DAG/MapReduce/图像管线等）
│  └─ build/                # 构建输出
└─ README.md                # 本文件
```

---

## 快速开始

### 依赖
- Linux + g++-12（C++17）
- CMake ≥ 3.18
- pthread（默认随 glibc 提供）
- GTest / spdlog（已在测试中使用）

### 构建与运行

```bash
# 构建 exper2
cd exper2
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j

# 运行示例程序（如存在）
./main

# 运行测试
./tests
```

> 注意：如果编译期间提示 `std::any` 不存在，请确保使用了 `-std=c++17`（CMake 已设置）。

---

## 设计与架构

### 总体流程

```
addMainTask / addSubTask
        │
        ├─> 维护主任务映射、子任务映射、名字→ID 映射
        │    以及“主任务→子任务列表”与“就绪队列（按优先级）”
        │
事件循环线程：EventDispatcher::start()
        │
        └─> EventLooper()：从就绪队列取子任务 → wrapSubTask() → 线程池执行
                               │
                               ├─ 等待所有依赖完成（完成事件 + 条件变量广播）
                               ├─ 执行用户函数，捕获异常
                               ├─ 写回 result / error → sem_post 唤醒取结果方
                               └─ 标记完成并 notify_all 依赖者；更新主任务计数

调用方：getSubTaskResult(name)
        ├─ 阻塞等待结果槽信号量
        ├─ 取出 std::any / 重新抛出异常
        └─ 清理子任务映射与 ID（延迟到此，避免破坏依赖检查）
```

### 核心模块

- `EventDispatcher<PoolSize>`（`exper2/EventDispatcher.hpp`）
  - 维护以下共享状态（均有互斥保护）：
    - 主任务：`main_tasks`、`main_task_name_to_id`、`main_to_subtasks`
    - 子任务：`sub_tasks`（值为 `std::shared_ptr<SubTask>`，避免 rehash 悬空）
    - 结果槽：`subtask_results`（值为 `std::shared_ptr<SubTaskResult>`）
    - 就绪队列：`ready_subtasks[Priority]`（按优先级多队列）
  - 事件循环线程 `start()/Stop()`：在 `is_running || hasPendingWork()` 条件下调度，安全退出
  - 依赖等待：`CompletionEvent` + `ConditionVariable::wait(lk, pred)`，完成后 `notify_all()` 广播
  - 结果回传：`std::any result` + `std::exception_ptr error` + `sem_t semaphore`
  - 清理策略：不在工作线程清理映射，统一在 `getSubTaskResult` 后清理，避免依赖被误判完成

- `Task.hpp`
  - `MainTask`：记录名称、优先级、子任务数、已就绪/完成计数（`std::atomic<size_t>`）
  - `SubTask`：名称、所属主任务名与 ID、依赖名列表、可调用对象 `std::function<std::any()>`
  - `CompletionEvent`：`Mutex + ConditionVariable + done`，用于依赖者等待
  - `SubTaskResult`：`std::any result + std::exception_ptr error + sem_t semaphore`

- `ThreadPool<N>`（`exper2/ThreadPool.hpp`）
  - `enqueue(F, Args...) -> std::future<invoke_result_t<...>>`
  - worker 捕获所有异常，避免 `std::terminate()` 杀死线程

- `RingQueue<Task, Size>`（`exper2/RingQueue.hpp`）
  - 生产/消费信号量：`full/empty` + 互斥保护 head/tail，支持 Stop()

- `Mutex/LockGuard/UniqueLock/ConditionVariable`
  - `ConditionVariable` 基于信号量实现 `wait/notify_one/notify_all`
  - 支持 `wait(UniqueLock&&)` 右值重载，保证返回时锁仍然“已持有”

---

## API 速览

- 增加主任务：
  ```cpp
  EventDispatcher<50>& d = EventDispatcher<50>::getInstance();
  d.start();
  d.addMainTask({"MainA", 3, Priority::HIGH});
  ```
- 增加子任务（带依赖）：
  ```cpp
  SubTask a{"A1", "MainA", [](){ return 42; }};
  SubTask b{"A2", "MainA", [](){ return std::string("ok"); }};
  b.dependencies.push_back("A1");
  d.addSubTask(std::move(a));
  d.addSubTask(std::move(b));
  ```
- 获取结果（阻塞）：
  ```cpp
  int v = std::any_cast<int>(d.getSubTaskResult("A1"));
  std::string s = std::any_cast<std::string>(d.getSubTaskResult("A2"));
  ```

> 子任务返回 void？`enqueue` 会返回 `std::future<void>`；本调度器内部统一收敛为 `std::any`，无返回值的任务可返回占位（例如 `std::monostate` 或不关心结果时忽略）。

---

## 示例：强依赖链（共享指针数据）

```cpp
auto buffer = std::make_shared<std::vector<int>>();
SubTask fill{"Fill", "MainA", [=]{ buffer->assign(1000, 1); return 1000; }};
SubTask x2  {"X2",   "MainA", [=]{ for (auto& v:*buffer) v*=2; return 0; }}; x2.dependencies = {"Fill"};
SubTask sum {"Sum",  "MainA", [=]{ long long s=0; for(auto v:*buffer) s+=v; return s; }}; sum.dependencies = {"X2"};
```

---

## 测试用例

`exper2/test.cc` 包含多组高强度用例，覆盖：
- 混合返回值（int/string/vector）、异常透传
- 依赖顺序保证（A→B→C）、长关键路径（链长 50）
- 三阶段屏障（Stage2 依赖全体 Stage1，Stage3 依赖全体 Stage2）
- 随机 DAG（FanIn/FanOut）、菱形依赖、网格分层屏障（5×5）
- MapReduce 词频（Load→Map→Shuffle→Reduce）
- 图像管线（解码→预处理→分块→聚合）
- 优先级偏置（HIGH 优先级抢先于 LOW）
- 指针共享链式计算（Fill→Transform→Sum）

运行：
```bash
cd exper2/build
./tests
```

---

## 常见问题与排障

- 编译报 “std 没有 any”
  - 需启用 C++17：`-std=c++17`
- “MainTask 拷贝构造/赋值被删除”
  - 内含 `std::atomic` 不可拷贝；请使用移动或容器中使用 `emplace`/`insert`，避免 `operator[]` 触发默认构造+赋值
- 取结果 `std::bad_any_cast`
  - `std::any_cast<T>` 的 `T` 必须与实际类型一致；不要尝试 `any_cast<std::future<T>>`
- `getSubTaskResult` 偶发卡住
  - 统一用 `shared_ptr<SubTaskResult>` 持有结果槽，锁外持有指针后再 `sem_wait`，避免 `unordered_map` rehash 导致悬空
  - 子任务异常需捕获并写入 `exception_ptr`，同时 `sem_post`；调用方重抛
  - 依赖等待使用“完成事件+广播”，不要用一次 `sem_post` + 多次 `sem_wait`
- 程序无法退出 / join 卡住
  - `start()` 循环应为 `while (is_running || hasPendingWork())`，`Stop()` 置位后等待跑空
  - 线程池析构前调用 `waitIdle()`（或确保队列为空且 worker 空闲）
  - 自定义 `thread` 的 `join()` 执行后应将句柄清零、标记 `m_joined=true`，析构检查 `joinable()` 才不触发 `terminate`

---

## 设计权衡

- 依赖同步：选择“完成事件+条件变量广播”而非“多次 sem_post”，从根上解决多依赖者的饿死问题
- 结果回传：`std::any` 提供统一接口，结合 `exception_ptr` 兼容异常路径
- 容器并发：不把迭代器/引用带出锁；跨锁使用 `shared_ptr` 指向的堆对象
- 清理时机：延迟到取结果后再清理映射，避免依赖检查失真

---

## 路线图（可选优化）
- 取消/超时：为依赖等待与 `getSubTaskResult` 增加超时版本、取消标志
- 任务重试与最大重试次数
- 拓扑级别批调度（依赖层级一次性下发）
- 观测性：任务级 trace、统计（排队时间/执行时间）、Prometheus 指标导出
- 工作窃取队列或多队列分级调度以提升吞吐

---

## 许可证
未声明（课程实验用途）。
