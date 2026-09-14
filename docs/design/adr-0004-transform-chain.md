# ADR-0004 — 变换链机制与"点 apply 变换"API

> **用途**：记录"连续变换如何写、如何被优化"的机制决策：链对象（记录/折叠/求值策略）、math 与 gfx 的分层、批量视图形态、2D/3D 类型分组。
> **目标读者**：维护 gfx_math 变换能力、实现几何/模型层、或接入批量后端的开发者与 AI Agent。
> **关联文档**：[adr-0002-template-math.md](adr-0002-template-math.md)；[adr-0003-batch-backend.md](adr-0003-batch-backend.md)；[transform.hpp](../../src/include/gfx/math/transform.hpp)；[batch.hpp](../../src/include/gfx/math/batch.hpp)；[api-docs.md](../conventions/api-docs.md)。

> **状态**：✅ 采纳（2026-09-09）。本 ADR 落地：`math::Transform<N>` 链对象 + `gfx::Point2D/Point3D`、`gfx::PointBatch2D/PointBatch3D`。

## 背景

- 现状：`transform2d::translate(...)` 等**立即求值返回矩阵**。写 `T * R * S * p` 时 `*` 左结合，强制先做 k-1 次矩阵乘再乘向量，对"单点"场景浪费（k=3,N=4：144 vs 最优 48）。
- 用户诉求：①连续变换应能"自然用上优化器"，无需调用方改写；②能**存一个变换**，需要时 `apply`；③写法上倾向"点/点批 apply 变换"（主体是几何数据）；④批点要零拷贝。

## 决策

### D1：链对象 `math::Transform<N>`（记录而非立即折叠）
- 记录步骤序列（`std::vector<Mat<N>>`，按书写顺序作用），提供 `then()` 追加、`operator*` 组合、`matrix()` 折叠、`apply()` 求值。
- **消费点决定策略**（这是"自然用上优化器"的关键）：
  - `matrix()` 折叠为**惰性 + 缓存**：追加步骤只记录、只让缓存失效；首次消费时折叠一次，之后直接复用；
  - 单点 `apply(p)`：内部即"单矩阵 apply"（1 次 mat-vec，折叠已被缓存）；
  - 批点 `apply(in,out,count,stride)`：复用同一折叠矩阵逐点（P·N²），连续布局再走后端钩子（SIMD/GPU）；
  - 未来：语义级融合（合并连续平移、跳过恒等步骤、按操作类型分派）——这是"链"独有、"持矩阵"方案做不到的优化空间。
- **边界（诚实声明）**：链中若全为同维方阵，乘法顺序不影响标量乘次数，"链序 DP"仅在维度不齐时有收益；本决策的收益来自**折叠 + 批量**，而非重排。

### D2：分层——机制在 math，几何语义在 gfx
- `math`：`Transform<N>` 链机制、`Vec::apply`、折叠与批量求值；`transform2d/3d` 工厂函数保留为底层**原语**。
- `gfx`：面向几何的流畅 API——`Point2D/Point3D` 与 `PointBatch2D/PointBatch3D`，方法链累积变换，最终一次性求值。
- 理由：矩阵/线性代数是数学；"点被变换"是图形几何概念；混在一起会污染纯数学类型。

### D3：主体方向 = 点/点批 `apply` 变换
- 单点：`p.apply(t)` 返回变换后的点（不改原值）；`value()` 用自身累积链求值。
- 批点：`points.apply(t, out)` 写外部输出缓冲；`out` 可与输入重叠（原地）。
- 变换对象本身可存储、可复制、可组合（`t1 * t2`，与 `then()` 同口径：**先 t1 后 t2**），以满足"存一个变换，随时 apply"。

### D4：批量数据 = 视图（零拷贝）
- 支持**连续**（`ptr + count`）与**带步长**（`ptr + count + strideBytes`，支持 AoS 顶点结构体交错布局）两种入口。
- 视图是纯 C++ 概念（指针 + 长度 + 步长），不涉及硬件；等距性是**调用方前置条件**（由 C++ 数组/结构体布局天然保证），不做运行时逐点校验。
- 分散内存（各自分配）无法零拷贝，需先 gather 或走单点路径。

### D5：2D/3D 分两组类型（不合并）
- `Point2D/PointBatch2D` 用 `Transform<3>` + 齐次 `Vec3(x,y,1)`；`Point3D/PointBatch3D` 用 `Transform<4>` + `Vec4(x,y,z,1)`。
- 理由：类型清晰、防误用；代价是两组类型与少量重复代码。
- 注：`Vec<N>` 恒 4 float 存储（ADR-0002）。`Transform<N>::apply(batch)` 要求输入点**已齐次**（N=4 的 w=1 / N=3 的 z=1）；"位置 → 齐次"的注入由 gfx 几何层（[point.hpp](../../src/include/gfx/point.hpp)）逐点完成，故 2D/3D 点批当前都走 CPU 逐点路径；将来若以齐次数组存储（`Vec4` 连续），可经 [batch.hpp](../../src/include/gfx/math/batch.hpp) 后端钩子改走 SIMD/GPU。

## 后果

- 新增公共 API：`math::Transform<N>`、`gfx::Point2D/Point3D`、`gfx::PointBatch2D/PointBatch3D`。
- 现有 `transform2d/3d` 原语与 `test_transform` 保持有效（非破坏）。
- 单点连续变换的标量乘法量下降（k 次 mat-vec 取代 k-1 次矩阵乘 + 1 次 mat-vec）。
- 批量求值统一为"折叠一次 + 逐点"，为将来 SIMD/GPU 后端留好插入点。

## 待细化

- 步长视图的 GPU 后端路径（2D 齐次注入的批量化）。
- 变换步骤按"操作类型"记录（如平移只做加法）以进一步省算力。
- 场景图/层级变换（先局部、后父级：`local.then(parent)` 或 `local * parent`）的 API 形态。

## 补充（2026-09-09，✅ 采纳）：统一"左乘口径"

- `Transform` 内部的组合读法统一为**书写顺序 = 作用顺序**：`then()`、流式调用、`a * b` 三处一致（`a * b` ≡ `a.then(b)` = 先 a 后 b）。
- 底层即"新步骤**左乘**到已累积矩阵上"（`matrix()` 折叠为 `s * acc`），故 **`matrix(a * b) == matrix(b) * matrix(a)`**。
- **代价（须记住）**：`Transform::operator*` 与 `Mat::operator*`（`A * B` 先作用 B）**互为镜像**。两套口径各自在自己类型内自洽；头文件注释与 glossary 已显式标注，避免混读。
- 修订原因：原实现 `a * b` 走"矩阵语义（先 b）"，与链式 `then()`/流式 API 的"从左往右"方向相反，调用方需在两套读法间来回切换。
- 验证：`tests/test_chain.cpp` 的组合语义块（`a * b` 结果、与 `then()` 等价、`matrix` 左乘关系）逐项断言。

## 补充（2026-09-09，✅ 采纳）：惰性折叠缓存 + 消除链复制

- `matrix()` 由"每次现算"改为**惰性折叠 + 缓存**（`mutable Mat<N> cached_` + `valid_`，`then`/`operator*` 使其失效）：
  - 追加仍只记录（零矩阵运算）；消费时折叠一次；之后每次 `apply` 都是 1 次 mat-vec。
  - 代价：`Transform` 多存一个矩阵与标志；`matrix()` 为 `const` 但改缓存 ⇒ **非线程安全**（已在注释标注）。
- 消除 `Point2D/3D::apply(t)` 与 `PointBatch2D/3D::apply(t,out)` 的**链复制**（原 `eff = t_; eff.then(t)` 每点 2 次堆分配）：
  改为 `t.matrix() * t_.matrix()`（一次矩阵乘，零分配），语义不变（外部变换最后作用）。
- 实测（N=100,000 点 / 3 步变换 / `-O2`，基线见 [benchmarks/bench_point_apply.cpp](../../benchmarks/bench_point_apply.cpp)）：
  - 逐点 `t.apply(p)`：**27.2 → 5.2 ns/点**（-81%，与"手工折叠一次"的 5.0 等价）；
  - 逐点 `p.apply(t)`：**46.3 → 9.0 ns/点**（-81%）；
  - 批量接口 3.6 ns/点仍最快。
