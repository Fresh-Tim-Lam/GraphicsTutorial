# benchmarks/ — 性能基准（正式，可复现）

> **用途**：存放**正式、可复现**的性能基准源码与基线数据，为性能类结论提供依据（见 [performance.md](../docs/conventions/performance.md)）。
> **目标读者**：改动热路径（变换构造、逐像素/逐点循环、批处理）的开发者与 AI Agent。
> **关联文档**：[performance.md](../docs/conventions/performance.md)（何时必须跑基准、结论如何记录）；[directory-organization.md](../docs/conventions/directory-organization.md)（与本目录/artifacts 的归属规则）；[tests/](../tests/README.md)（功能正确性）。

> 状态：生效中（核验于 2026-09-09）。

## 与 tests/、artifacts/ 的分工

| 区域 | 用途 | 是否入库 |
|------|------|---------|
| [benchmarks/](README.md) | **正式基准**：性能结论的依据，可复现 | ✅ 入库 |
| [tests/](../tests/README.md) | 功能正确性（断言） | ✅ 入库 |
| [artifacts/](../artifacts/README.md) | 一次性调试/试探脚本与产物 | ❌ gitignore |

结论依据必须是 benchmarks/ 里的基准；`artifacts/` 里的临时脚本一旦成为结论依据，须移入本目录。

## 文件清单（索引自维护）

| 文件 | 测量对象 | 构建/运行（仓库根执行） |
|------|---------|----------------------|
| [bench_rotate.cpp](bench_rotate.cpp) | `transform3d` 变换构造：绕轴 / 绕点 / 组合版 / 换基版 | `g++ -O2 -std=c++17 -I src/include benchmarks/bench_rotate.cpp -o benchmarks/bench_rotate.exe` 然后 `benchmarks\bench_rotate.exe` |
| [bench_point_apply.cpp](bench_point_apply.cpp) | 点变换消费路径：逐点 `t.apply(p)` / 折叠+逐点 / 批量接口 / 逐点流式链 | `g++ -O2 -std=c++17 -I src/include benchmarks/bench_point_apply.cpp -o benchmarks/bench_point_apply.exe` 然后 `benchmarks\bench_point_apply.exe` |
| [bench_fold.cpp](bench_fold.cpp) | 变换链折叠路径：折叠内核（首步赋值/免别名内核）、进链 `push_back`（有无 reserve）、建链+首消费端到端 | `g++ -O2 -std=c++17 -I src/include benchmarks/bench_fold.cpp -o benchmarks/bench_fold.exe` 然后 `benchmarks\bench_fold.exe` |

## 基线数据

各基准的**基线数字与测量条件**（编译器、优化档、迭代次数、日期）写在**对应源文件头部**（单一事实源），本 README 不复制，避免双源漂移。
