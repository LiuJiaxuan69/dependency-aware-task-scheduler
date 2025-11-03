# 功能测试用例索引（exper2/functional_tests/test.cc）

说明：本索引由测试源码中的中文注释汇总而成，按用例名称列出每个测试的目的、做法与期望结果。测试套件：`DispatchEventTest`。

---

## HandlesMixedReturnValues
- 目的：验证调度器可处理不同返回类型（int、std::string、std::vector<int>）的子任务且结果类型正确。
- 做法：同一 MainTask 下提交三种不同返回值的 SubTask，分别获取并 any_cast 校验。
- 期望：类型与数值匹配（42、"ResultFromTask2"、向量首元素为 1 且长度为 4）。

## RespectsDependencyOrdering
- 目的：验证依赖顺序是否被严格遵守（B 依赖 A，A 必先于 B 执行完成）。
- 做法：构造 A→B，两任务向 execution_order 记录启动顺序，等待结果后断言顺序。
- 期望：获取结果 A=="A done"、B==7，且执行顺序为 A 在前、B 在后。

## HandlesHighConcurrencyLoad
- 目的：在较高并发负载（多个 MainTask，每个多个 SubTask）下验证结果汇总正确且无死锁。
- 做法：批量提交任务，子任务返回唯一整型负载值，汇总所有返回值。
- 期望：观察到的和等于预期和（全量任务均正确运行并完成）。

## DiamondDependencyGraph
- 目的：验证“菱形依赖”拓扑（A→B、A→C，{B,C}→D）的拓扑执行顺序正确。
- 做法：A/B/C/D 记录到达顺序，等待 D 的结果。
- 期望：A 必先于 B/C，且 D 在 B/C 之后，D 的返回值为 23（仅用于校验流程串联）。

## ThreeStagePipelineWithBarriers
- 目的：验证分阶段流水线的屏障语义（S2 全依赖 S1，S3 全依赖 S2）。
- 做法：构造三阶段任务，记录执行顺序，等待聚合任务完成。
- 期望：所有 S1 先于任何 S2；所有 S2 先于 S3；最终结果字符串为 "OK"。

## LongCriticalPathChain
- 目的：验证长关键路径链（T0→T1→...→T49）在队列和线程池下不会死锁，并保持单调顺序。
- 做法：构造线性依赖链，仅拉取末尾结果，检查记录顺序单调递增。
- 期望：最终结果为 49，执行顺序递增，无乱序。

## RandomDAG_FanInFanOut
- 目的：验证随机 DAG（节点 i 随机依赖 [0..i-1] 的若干节点）能被正确拓扑执行。
- 做法：构造 N 个节点并随机添加依赖，再添加依赖所有节点的聚合器。
- 期望：聚合器返回 "DAG OK"，说明 DAG 全部可达且按依赖完成。

## MultipleMainTasksIsolation
- 目的：验证多个 MainTask 并存时的隔离与各自内部依赖顺序的正确性。
- 做法：构造 A: A1→A2→A3 与 B: B1→B2→B3 两条链，交错入队执行。
- 期望：两条链互不干扰，且各自链内顺序严格递增，最终 A3==3、B3==30。

## PrioritySchedulingBias_HighBeforeLow
- 目的：验证优先级调度偏好：高优先级任务应更早被执行/启动于低优先级任务。
- 做法：先入队高优先级 HP_*，再入队低优先级 LP_*（附加轻微延时），记录启动顺序。
- 期望：首个启动应为 HP_*，且第一批 HP_* 的首次出现索引小于 LP_*。

## GridBarrier_5x5_RowsAreBarriers
- 目的：验证二维网格逐行屏障：第 r 行所有任务依赖第 r-1 行所有任务。
- 做法：为每个 (r,c) 任务添加对上一行所有列的依赖，记录执行顺序。
- 期望：行 r 的所有任务完成索引均小于行 r+1 的任一任务的首次出现索引。

## LargeFanOutFromRoot_BroadcastDependencies
- 目的：验证单根广泛扇出（ROOT → 20 个子任务）的依赖广播不会出现饥饿或遗漏。
- 做法：ROOT 完成后，所有子任务仅依赖 ROOT；统计子任务启动数量，并拉取全部子任务结果。
- 期望：所有子任务均被执行且返回 "CH_*"，累计启动数等于子任务总数。

## ExceptionPropagationFromSubTask
- 目的：验证子任务抛出的异常可通过 getSubTaskResult 正确传播到调用方。
- 做法：提交一个正常任务 OK 和一个抛异常的任务 BOOM，分别获取结果。
- 期望：OK 返回 123；获取 BOOM 结果时抛出 std::runtime_error。

## PointerSharedDataChain_StrictOrdering
- 目的：验证通过共享指针传递的内存缓冲在依赖链上能保持严格的先后顺序与数据一致性。
- 做法：PC_Fill 写入缓冲，PC_X2 原地放大2倍，PC_Sum 汇总求和，逐个取回结果校验。
- 期望：填充量等于 N，首尾值符合放大预期，总和为 N*(N-1)，缓冲中部分数据点被正确更新。

## ImagePipeline_TilingAggregation
- 目的：验证“解码→预处理→分块→聚合”的影像流水线能按依赖阶段完成且结果正确。
- 做法：Decode 生成全 1 图像，Pre 将其×2；将图像分块求和写入 tilesum，最后聚合求总和。
- 期望：总和等于 W*H*2。

## MapReduce_WordCount
- 目的：模拟 MapReduce 的词频统计，验证多阶段（Load/Map/Shuffle/Reduce）依赖编排正确。
- 做法：Load 产生分片文本；每个 Map 统计词频；Shuffle 合并；Reduce 读取 "apple" 计数。
- 期望：最终 apple 计数等于各分片累计期望值。

## DL_MicroTrainingPipeline
- 目的：模拟一个极简的深度学习训练流水（Load→Aug→Assemble→Forward→Backward→Update）。
- 做法：Aug 将样本做线性变换，Assemble 求和，Forward 计算 loss，Backward 占位，Update 按 lr 更新参数。
- 期望：最终参数与解析解 1 - 0.002*B^2 在 1e-6 精度内一致。

## PairwiseCombine_MultiLayerDAG
- 目的：验证分层配对合并的多层 DAG（Stage0→Stage1 配对求和→Stage2 聚合）的正确性。
- 做法：Stage0 生成 1..K；Stage1 成对相加；Stage2 对 Stage1 结果求和。
- 期望：总和为 21（当 K=6 时：3+7+11）。

## PriorityScheduling_GlobalCounterAggregation
- 目的：验证在所有优先级同时解锁（最后一个子任务提交后统一就绪）时，调度器会优先运行高优先级主任务的子任务，
	通过共享全局原子计数器的“观测和”反映先后顺序——高优先级的和应更小。
- 做法：为每个优先级各创建一个主任务（每个 M 个子任务）。先为每个主任务提交 M-1 个子任务，暂不触发运行；
	最后再提交每个主任务的第 M 个子任务，使得七个主任务几乎同时就绪。所有子任务执行体对同一个原子计数器自增并返回增后的值；
	收集每个主任务的所有返回值并求和，比较不同优先级的总和。
- 期望：sum(HIGHEST) < sum(HIGH) < sum(ABOVE_NORMAL) < sum(NORMAL) < sum(BELOW_NORMAL) < sum(LOW) < sum(LOWEST)。
