# design/ — 设计文档与决策记录（ADR）

> **用途**：存放设计文档与架构决策记录（ADR）：系统/模块设计、重构方案、决策归档。
> **目标读者**：进行设计讨论与决策追溯的开发者 / AI Agent。
> **关联文档**：[docs/README.md](../README.md)；[meta-rules.md](../conventions/meta-rules.md)（结论落盘纪律）。

> 状态：生效中（核验于 2026-09-09）。初始为空，随首个模块设计讨论生长。

## 导航（索引自维护）

| 文档 | 内容 |
|------|------|
| [adr-0001-gfx-math-canvas.md](adr-0001-gfx-math-canvas.md) | 首个模块决策：gfx_math 头文件子库 + Canvas 帧缓冲（✅ 采纳 2026-09-09） |
| [adr-0002-template-math.md](adr-0002-template-math.md) | gfx_math 全模板化：Mat&lt;N&gt;/Vec&lt;N&gt; + 别名，算法收敛 math::detail（✅ 采纳 2026-09-09） |
| [adr-0003-batch-backend.md](adr-0003-batch-backend.md) | GPU 切换预留分派点：batch 接口 + 运行时后端钩子，零依赖（✅ 采纳 2026-09-09） |
| [adr-0004-transform-chain.md](adr-0004-transform-chain.md) | 变换链机制 + "点 apply 变换" API：math 机制 / gfx 几何层，视图零拷贝（✅ 采纳 2026-09-09） |
| [adr-0005-raster-polygon-fill.md](adr-0005-raster-polygon-fill.md) | 光栅化模块：Bresenham 画线 + 扫描线填充（ET/AET + 半开区间规则）+ 自写 PNG 输出（✅ 采纳 2026-09-14） |
| [adr-0006-camera-view.md](adr-0006-camera-view.md) | 相机/视图：世界坐标 ↔ 像素坐标映射（2D 正交等比 + fit、三条入口同一公式；3D 观察/投影 + 近平面裁剪 + 视体参数/NDC 深度/投影分解/反投影射线）（✅ 采纳 2026-09-16） |
| [adr-0007-math-transform-completion.md](adr-0007-math-transform-completion.md) | gfx_math 变换补全（第三讲知识点）：错切、逆变换闭式、向量变换（w=0）、齐次归一化、欧拉角、带原点坐标基、仿射矩阵拆解（✅ 采纳 2026-09-16） |
| [adr-0008-triangle-raster.md](adr-0008-triangle-raster.md) | 三角形光栅化：采样判定 + top-left 边界规则 + 重心坐标 + 包围盒/增量填充 + 抗锯齿（覆盖率 / SSAA）+ 深度缓冲（✅ 采纳 2026-09-16） |
| [adr-0009-halfedge-euler-ops.md](adr-0009-halfedge-euler-ops.md) | 半边数据结构（B-rep）+ 五个欧拉操作（MVFS/MEV/MEF/KEMR/KFMRH）：索引池、环链维护规则、校验策略（✅ 采纳 2026-09-16） |
| [adr-0010-line-algorithms.md](adr-0010-line-algorithms.md) | 三种画直线算法对照（Naive / DDA / Bresenham）：`LineAlgorithm` 选择 + **默认 Bresenham**（纯整数/无除法/无浮点）（✅ 采纳 2026-09-28） |
| [adr-0011-ray-geometry.md](adr-0011-ray-geometry.md) | 光线求交与包围体加速（作业三沉淀）：`Ray`/`Aabb`/`HitRecord` + 球/平面/三角形/圆柱/AABB 求交（Möller–Trumbore、slab）+ `Bvh`（中位数划分 + 剪枝）+ `math::reflect/refract`，全部落 `geom/`（✅ 采纳 2026-10-08） |
| [adr-0012-raytracing-lighting.md](adr-0012-raytracing-lighting.md) | 光照模型与递归光线跟踪（作业三再沉淀）：`Material`/`Light`/`shadeLocal`（局部光照 + 阴影回调）+ `SurfaceHit`/`RayTracer`（反射/折射/递归/超采样/伽马）；求交与阴影均以**回调**与场景解耦，落 `render/`（单向依赖 `geom/`）（✅ 采纳 2026-10-09） |
| [adr-0013-normal-mapping.md](adr-0013-normal-mapping.md) | 纹理与法线贴图：`Texture`（环绕/滤波采样）与 `TangentBasis`/`perturbNormal`/`makeNormalMap`（高度场→法线图）分两层落 `render/`；切空间用解析解；经 `SurfaceHit::shadingNormal` 注入（**几何法向管光线数学、着色法向管光照**），为将来光栅化着色管线预留复用（✅ 采纳 2026-10-09） |
| [adr-0014-shaded-raster-pipeline.md](adr-0014-shaded-raster-pipeline.md) | 光栅化着色管线：`VertexAttributes`/`Fragment`/`FragmentShader`/`fillTriangleShaded` —— 顶点属性 + **透视校正插值** + 近平面裁剪 + 逐像素着色（着色以**回调**注入，与光照解耦）；兑现 ADR-0013 的复用（纹理/法线贴图在光栅化路径上原样可用）（✅ 采纳 2026-10-09） |
