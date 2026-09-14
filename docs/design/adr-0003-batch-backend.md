# ADR-0003 — GPU 后端切换的预留分派点（batch 接口 + 运行时钩子）

> **用途**：记录"为 GPU（未来 CUDA / 自研后端）保留切换接口"的结构性决策：接口形态、当前范围、与零依赖约束的关系。
> **目标读者**：后续接入 GPU 后端、或使用批量变换的开发者与 AI Agent。
> **关联文档**：[adr-0001-gfx-math-canvas.md](adr-0001-gfx-math-canvas.md)；[adr-0002-template-math.md](adr-0002-template-math.md)；[batch.hpp](../../src/include/gfx/math/batch.hpp)；[api-docs.md](../conventions/api-docs.md)。

> **状态**：✅ 采纳（2026-09-09）。当前实现 = CPU 默认 + 运行时钩子（可注入自定义后端），零第三方依赖；CUDA 为后续外部后端，未在本仓库接入。

## 背景

- matmul 单次小矩阵上 GPU 只会更慢；GPU 价值在**批量**（一批点/顶点同矩阵变换）。
- 故"GPU 切换接口"不放在 `operator*` 层，而放在**批量变换层**。
- 项目约束：零第三方依赖、仅基础 Windows 像素着色、从 0 造轮子。直接引 CUDA 会打破约束，且本环境（MinGW + 无 NVIDIA 工具链）无法验证。

## 决策

### D1：新增 gfx::math 批量变换接口（batch.hpp）
- `batchTransform<N>(m, in[], out[], count)`：对 count 个 Vec\<N\> 做 `v' = m*v`。
- 数据布局：每点连续 4 float（Vec\<N\> 恒 4 分量对齐，见 ADR-0002），仅前 N 分量参与，GPU/SIMD 友好。

### D2：运行时后端钩子（"切换 GPU"的入口）
- `BatchTransformFn<N>` = 后端函数指针签名。
- `setBatchTransformBackend<N>(fn)` 注册/替换后端；`reset...`/传 `nullptr` 恢复 CPU 默认；`batchTransformBackend<N>()` 查询。
- 内置 CPU 后端逐点实现；未来 CUDA / 自研后端以同一签名注入，**调用方代码不变**。

### D3：本仓库现阶段不接 CUDA（范围锁定）
- 原因：依赖例外未授权到代码层 + 本机无法编译/验证 + 教学阶段无真实批量热点。
- 后续接入方式：外部/分支内提供 `gfx::math::cuda` 后端模块（依赖 nvcc），调用 `setBatchTransformBackend`；届时按 meta-rules 更新 ADR 与本文件。

## 后果

- 新增公共头 batch.hpp 与 3 个模板函数；零依赖不变。
- 批量变换是未来"顶点批量变换/光栅化前处理"的热点形状，接口先定死可避免 API 漂移。
- 不承诺性能：CPU 后端为教学朴素实现，未 SIMD（见 ADR-0002 结论：小形状展开即可，大批量再优化）。

## 待细化

- 真实 CUDA 后端（独立于本库，需工具链）；后续如需可再评估。
- 批量的 SIMD 路径（同函数签名，未来在 detail 层加分派）。

## 补充记录（2026-09-09，✅ 采纳为解释性结论）

- **批量变换 = 大矩阵相乘**：把 M 个齐次顶点拼成 M×4 大矩阵后，整批变换 `V' = V × Mᵀ` 可表达为一次 GEMM（矩阵乘法是 CPU/GPU 优化最充分的原语）。batchTransform 的后端分派点未来即可填入"基于 GEMM/SIMD/GPU 的实现"，公共接口不变。
- **vec 与 mat 不合并**：二者是语义/类型安全划分（点/方向 vs 变换/算子），与"小矩阵 CPU 难以优化"无关；合并不产生性能收益，反而丢失编译期维度防错与可读性。
- 大型三维模型的**存取与顶点缓冲**（连续顶点数组 + 加载 → 整批变换）属于模型数据层，归入 todo.md 待办，不在矩阵库实现。

## 复核记录（2026-09-09，✅ 结论：无需改动设计）

针对"预留矩阵拼接"重审：目标形态确认为**单矩阵 × 整批顶点**（一个 4×4 变换整批顶点），
- 该语义已由 `batchTransform` 完整覆盖；
- 其数据布局（每点连续 4 float、行主序）即 **M×4 大矩阵**，运算等价行主序 GEMM：`Out(M×4) = In(M×4) × mᵀ(4×4)`，与未来 GEMM/SIMD/GPU 后端形状一致，接口与布局无需变更；
- 不引入"拼接/大矩阵"类型；矩阵库维持现状。剩余动作仅为：GEMM/SIMD 后端实现 + 模型数据层（均已在 todo.md）。
