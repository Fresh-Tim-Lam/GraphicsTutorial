# tutorials/ — 教学树（树状组织，非瀑布编号章节）

> **用途**：存放 GraphicsTutorial 的教学/说明文档与配套可运行示例。教学按**树状主题**组织（从底层像素着色的"枝叶"逐级生长为渲染 API 的"主干"），不使用"第 1 章…第 N 章"瀑布编号。
> **目标读者**：学习从 0 造轮子的读者；编写/维护教学的开发者与 AI Agent。
> **关联文档**：[src/](../src/README.md)（权威实现）；[docs-management.md](../docs/conventions/docs-management.md)（docs 组织）；[AGENT.md](../AGENT.md)。

> 状态：生效中（核验于 2026-09-09）。首棵子树随首个模块落地时生长。

## 职责一句话

教学树 = 每节点一个主题目录：`README.md` 讲解（该主题实现思路/原理）+ `code/`（该主题可运行的演示/练习代码）+ 可嵌套子主题目录。

## 组织规则

- **树状而非编号**：主题靠目录嵌套表达依赖/演进关系（如 `window/`、`window/swapchain/`），禁止用 `01-`、`02-` 编号前缀制造虚假线性。
- 每节点含 `README.md`（首段即该主题"是什么/为什么"），可含 `code/`（演示代码，通常小到可单文件跑通）。
- 主题目录除 `README.md` 与 `code/` 外，可按需含：`data/`（样例输入）、`output/`（结果图）、`submit/`（作业/对外提交素材与独立构建脚本）。
- **单一事实源**：教学 `code/` 内的代码为演示/练习用途；同名能力的**权威实现归属 src/**。教学文档需引用权威实现位置，标注"权威实现见 `src/...`"，教学代码与 src 冲突时以 src 为准。
- 代码语言：C++ 标识符英文、注释中文（见 [naming.md](../docs/conventions/naming.md)）。
- 构建：教学 code 若需构建，随 modules 加入根 CMake 的示例目标（骨架期不虚构目标）。

## 树形导航（将随生长更新）

- 根节点：此 README.md。
- [pixels-on-screen/](pixels-on-screen/README.md) —— 把第一个像素画到屏幕（Canvas + Window，第一个可见成果）。
- [math-foundations/](math-foundations/README.md) —— 向量与矩阵：渲染的地基（gfx_math 使用与原理）。
- [polygon-scan/](polygon-scan/README.md) —— 扫描线多边形填充：画线（Bresenham）+ 边表/活性边表填充 + 自写 PNG 输出（作业 1）。
- [view-projection/](view-projection/README.md) —— 相机与视图：世界坐标 ↔ 像素坐标（`gfx::View`，2D 正交等比映射/contain + 3D 投影链路）。
- [projection-3d/](projection-3d/README.md) —— 正交投影 vs 透视投影：3D 观察/投影/近平面裁剪（线框立方体左右同框对比）。
- [triangle-raster/](triangle-raster/README.md) —— 三角形光栅化：采样判定 + top-left 边界规则 + 重心坐标 + 抗锯齿（覆盖率 / SSAA）+ 深度缓冲。
- [euler-ops/](euler-ops/README.md) —— 半边数据结构（B-rep）+ 五个欧拉操作（MVFS/MEV/MEF/KEMR/KFMRH）：实体拓扑的逐步构造与校验。
- [line-algorithms/](line-algorithms/README.md) —— 三种画直线算法对照（Naive / DDA / Bresenham）：每步运算量对照、并排成图 + 逐线段像素统计。
- [ray-basics/](ray-basics/README.md) —— 光线求交地基（作业三）：主光线 + 球/三角形/圆柱/平面求交 + BVH，法向可视化（只用求交、不用光照）。
- [ray-tracing/](ray-tracing/README.md) —— 光线跟踪：光照 + 反射/折射 + 阴影 + BVH + 超采样，用 gfx 自己渲染一张图；同目录还有**窗口交互版**（实时调相机）。
- [ray-tracing-framework-scene/](ray-tracing-framework-scene/README.md) —— 用 gfx 复刻**课程框架（作业三）的原始场景**，与框架基准图做**逐像素等价性比对**。
- [normal-mapping/](normal-mapping/README.md) —— 法线贴图：贴图采样 + 切空间 TBN + 程序化高度场，把光滑表面做出凹凸（并观察凹凸如何扭曲镜面反射）。
- [shaded-raster/](shaded-raster/README.md) —— 光栅化着色管线：顶点属性 + **透视校正插值** + 逐像素着色 + **逐光源阴影贴图**，把同一套纹理/切空间用在**三角形光栅化**路径上（GPU 里真正跑的那条路）；同目录还有**窗口交互版**（实时调相机 + 法线贴图/阴影开关）。

### 教学演示目标（CMake）

| 目标 | 内容 | 运行方式 |
|------|------|---------|
| tutorial_pixels | 像素上屏演示（对角线+定点） | `cmake --build build --target tutorial_pixels` 后运行 `build/tutorials/tutorial_pixels` |
| tutorial_math | 矩阵旋转/平移数学演示 | `cmake --build build --target tutorial_math` 后运行 `build/tutorials/tutorial_math` |
| tutorial_polygon_fill | 多边形扫描转换（画线 + 扫描线填充 + PNG 输出 + 窗口动画），产物名 `PolygonFill` | `cmake --build build --target tutorial_polygon_fill` 后运行 `build/tutorials/PolygonFill --help` |
| tutorial_projection_3d | 正交 vs 透视投影（线框立方体同框对比 + 近平面裁剪演示），产物名 `Projection3D` | `cmake --build build --target tutorial_projection_3d` 后运行 `build/tutorials/Projection3D --help` |
| tutorial_triangle | 三角形光栅化（点采样 vs 覆盖率 AA vs SSAA 对比 + 乱序深度测试），产物名 `TriangleRaster` | `cmake --build build --target tutorial_triangle` 后运行 `build/tutorials/TriangleRaster --help` |
| tutorial_euler | 欧拉操作（四面体连续构造 + KEMR 内环 + KFMRH 内环，逐步拓扑校验并渲染线框），产物名 `EulerOps` | `cmake --build build --target tutorial_euler` 后运行 `build/tutorials/EulerOps --help` |
| tutorial_line_algorithms | 三种画直线算法对照（Naive / DDA / Bresenham 并排成图 + 逐线段像素统计），产物名 `LineAlgorithms` | `cmake --build build --target tutorial_line_algorithms` 后运行 `build/tutorials/LineAlgorithms --help` |
| tutorial_ray_basics | 光线求交地基（主光线 + 球/三角形/圆柱/平面求交 + BVH，法向可视化、无光照），产物名 `RayBasics` | `cmake --build build --target tutorial_ray_basics` 后运行 `build/tutorials/RayBasics --help` |
| tutorial_ray_tracing | 光线跟踪（光照 + 反射/折射 + 阴影 + BVH + 超采样，用 gfx 自己渲染），产物名 `RayTracing` | `cmake --build build --target tutorial_ray_tracing` 后运行 `build/tutorials/RayTracing --help` |
| tutorial_ray_tracing_interactive | 光线跟踪**交互**演示：窗口内实时调相机（拖动转视角 / WASD 移动 / 滚轮推拉 / 空格出图），产物名 `RayTracingInteractive` | `cmake --build build --target tutorial_ray_tracing_interactive` 后运行 `build/tutorials/RayTracingInteractive` |
| tutorial_normal_mapping | 法线贴图（对照组球 / 砖缝 / 波纹 + 棋格地面；程序化生成法线图 + TBN 扰动，含 A/B 对照），产物名 `NormalMapping` | `cmake --build build --target tutorial_normal_mapping` 后运行 `build/tutorials/NormalMapping --help` |
| tutorial_shaded_raster | 光栅化着色管线（地面/立方体/球全由三角形拼出；透视校正插值 + 逐像素采样法线贴图 + `shadeLocal` + **逐光源阴影贴图**），产物名 `ShadedRaster` | `cmake --build build --target tutorial_shaded_raster` 后运行 `build/tutorials/ShadedRaster --help` |
| tutorial_shaded_raster_interactive | 光栅化着色管线**交互**演示：窗口内实时调相机（拖动转视角 / WASD 移动 / 滚轮推拉 / `1` 切法线贴图 / `2` 切阴影 / 空格出图），产物名 `ShadedRasterInteractive` | `cmake --build build --target tutorial_shaded_raster_interactive` 后运行 `build/tutorials/ShadedRasterInteractive` |
| tutorial_hw3_scene | 复刻课程框架（作业三）原始场景并与框架基准图逐像素比对，产物名 `Hw3SceneTrace` | `cmake --build build --target tutorial_hw3_scene` 后运行 `build/tutorials/Hw3SceneTrace --help` |
