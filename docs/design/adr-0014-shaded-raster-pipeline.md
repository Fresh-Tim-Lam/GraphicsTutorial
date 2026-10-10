# ADR-0014 — 光栅化着色管线（顶点属性 + 透视校正插值 + 逐像素着色）

> **用途**：记录"光栅化路径的着色管线"落位 `gfx` 的决策：文件落点、顶点属性的空间约定、透视校正的插值口径、近平面裁剪、逐像素着色如何与光照解耦、以及它如何让 ADR-0013 的纹理/法线贴图在**光栅化**路径上直接复用。
> **目标读者**：实现/使用光栅化着色的人与 AI Agent。
> **关联文档**：[adr-0013](adr-0013-normal-mapping.md)（纹理与法线贴图）、[adr-0008](adr-0008-triangle-raster.md)（三角形光栅化基线）；实现 [shading.hpp](../../src/include/gfx/render/shading.hpp)；测试 [test_shading.cpp](../../tests/test_shading.cpp)；教学主题 [shaded-raster](../../tutorials/shaded-raster/README.md)。

> **状态**：✅ 采纳（2026-10-09）。落地：`VertexAttributes` / `Fragment` / `FragmentShader` / `ShadingTriangle` / `fillTriangleShaded`（shading.hpp）。

## 背景

- 触发材料：ADR-0013 把"纹理 + 切空间 + 法线扰动"做成了**与渲染路径无关**的公共件，但当时只接上了光线跟踪（靠 `SurfaceHit::shadingNormal`）。法线贴图的典型用法其实在**光栅化**路径上。
- 项目现状：`triangle.hpp` 只有"填纯色 + 抗锯齿"，边缘函数算出的重心坐标**只用于覆盖判定**，没有顶点属性、没有属性插值、没有逐像素着色；`depth.hpp` 有深度缓冲；`view.hpp` 有完整的世界→像素投影链路（含近平面裁剪的 `projectSegment`）。
- 缺口：要跑通"顶点带 uv/法向/切线 → 光栅化 → 逐像素采样贴图 + 光照"，需要新加一条**着色管线**（这是 ADR-0013 里说的"路径 B"）。

## 决策

### D1：新建 `render/shading.hpp`，与 triangle.hpp / depth.hpp 并列

- 只依赖 `core/`（Canvas/Color）+ `math/`，并**复用**既有光栅化件：`insideTriangle` / `barycentric`（覆盖判定与重心坐标，含 top-left 边界规则）与 `DepthBuffer::testAndSet`。
- 理由：光栅化器（怎么决定"覆盖哪些像素"）与着色管线（"每个覆盖到的像素算什么颜色"）是两件事；混进 `triangle.hpp` 会让那个文件同时承担两条职责。

### D2：顶点属性用**世界空间**；投影自算（不改 `View` 的公开 API）

- `VertexAttributes` 的 `position` / `normal` / `tangent` 都是世界空间；投影用 `View::viewProjectionMatrix()`。
- **需要裁剪空间 w**（透视校正的前提），而 `View::clipToPixel` 是私有的。本管线选择：自己按 `view.viewportX/Y/Width/Height()` 复刻同一套"裁剪空间 → 像素"映射（公式与 `View::clipToPixel` 逐字一致：透视除法 + 视口 + y 翻转），**而不是**为了内部需要给 `View` 开新公开 API。
- 理由：`View` 的公开面保持"单点投影 / 线段投影"这两个自然入口；着色管线的需要（拿 w）属于实现细节，复制 3 行映射比扩大公共 API 更划算（用测试锁定两处映射一致）。

### D3：插值口径 —— 属性做**透视校正**，深度**不做**

- 属性：在屏幕空间线性插值 `attr/w` 与 `1/w`，再相除（等价说法：权重 `k_i = (λ_i / w_i) / Σ(λ_j / w_j)`）。
- 深度：NDC z 在屏幕空间**本就是线性的**（z_ndc 是 1/w 的仿射函数），故直接用未校正权重 `λ_i` 插值 —— 与 `depth.hpp` 的既有实现口径一致。
- 用**不变式**锁定：每个被着色像素，把片元的世界位置重新投影回去必须仍落在该像素中心（正交投影下自然成立；透视投影下若误用仿射插值会偏差几十像素）。

### D4：近平面裁剪（Sutherland–Hodgman 单平面）

- 在裁剪空间按 `w ≥ 近平面` 裁剪：`w` 阈值取 `View::frustum().nearZ`（透视），正交下 `w ≡ 1` 故取小量即可。裁剪产生 0..4 个顶点的凸多边形，再扇形三角化为 1~2 个三角形。
- 新顶点的属性随**同一个裁剪参数 t** 线性插值 —— 因为"世界坐标 → 裁剪空间"是**线性（齐次）映射**，所以在裁剪空间插值 t 与在世界空间插值 t 等价。
- 为什么必须裁：透视下相机后方的点齐次 `w < 0`，直接做透视除法会把它**镜像翻进画面**（与 `view.hpp` 里 `projectSegment` 必须裁的理由完全相同）。

### D5：逐像素着色以**回调**注入（本层不认识光照模型）

- `FragmentShader = std::function<math::Vec3(const Fragment&)>`；`Fragment` 携带**透视校正后**的 `position / normal / tangent / uv` + `depth` + `invW`。
- 本层不 include `lighting.hpp`、不认识 `Material`/`Light`/贴图 —— 与 `raytracer.hpp` 的求交回调（`HitFunction`）、`lighting.hpp` 的阴影回调（`ShadowTest`）是同一条分层纪律。
- 收益：同一套管线能配任意着色（Blinn-Phong / 贴图 / 法线贴图 / 程序化纹理），互不牵扯。

### D6：兑现 ADR-0013 的复用 —— 法线贴图在光栅化路径上直接可用

- 片元里用 `texture.hpp` 采样 → `normalmap.hpp` 的 `decodeNormal` / `perturbNormal` 扰动法向 → 交给调用方的着色回调。
- **片元里要重建一次正交基**：插值后的 `normal` 与 `tangent` 各自被透视校正拉过，不再严格正交；做法是先把 normal 归一、再用 `orthogonalize` 把 tangent 投影到其正交补，最后 `bitangent = cross(normal, tangent)`。

### D7：已知边界（刻意不做，避免过度设计）

- **不做背面剔除**：闭合网格靠深度测试即可得到正确结果（背面先画、被前面覆盖）。要剔除需要约定屏幕空间绕序，留待有明确需求时再加。
- **不做 alpha / 混合写入**：着色回调返回 RGB，写入用 `putPixel`（覆盖）。
- **颜色量化不做 gamma**：`shading.hpp` 只把 0..1 钳制并四舍五入到 8bit；色彩空间变换由调用方在着色回调里决定（演示里在 shader 内 `pow(1/2.2)`）。

## 验证（可复现）

- 单元测试 `ctest -R test_shading`（**17340 checks，0 failures**）：
  1. **覆盖一致性**：同一组屏幕坐标下，着色管线的覆盖像素与既有 flat 光栅化器（`fillTriangle` 的 `SolidBBox` 模式）**逐一相同（diff = 0）**；
  2. **正交 ⇒ 仿射**：单位投影下 uv 等于屏幕空间重心坐标（逐像素比对）；
  3. **透视校正不变式**：深跨度大的斜三角形，每个片元的世界位置重投影到对应像素中心的误差 < 0.05 px；
  4. **深度测试**：远近三角形乱序绘制结果**完全一致**，两者可见部分都保留；
  5. **近平面裁剪**：跨界三角形仍满足不变式（实测最大误差 **0.0000 px**），整块在相机后方时不绘制；
  6. **属性透传**：normal/tangent/uv/position 均到达片元；空着色器不绘制。
- `ctest --test-dir build`：**15/15 通过**（原 14 + `test_shading`）。
- 教学演示实跑（800×600，地面 6×6 四边形 + 立方体 6 面 + 球 28×44 经纬网格 = **2478 个三角形**、**436550 个着色片元**）：开/关法线贴图（`--strength 1` vs `0`）**259715 / 480000 像素不同（54.11%）**，最大通道差 171 —— 说明贴图确实经"采样 → TBN 扰动 → 逐像素光照"起了作用。
