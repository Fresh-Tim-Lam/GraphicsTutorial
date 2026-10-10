# ADR-0006 — 相机/视图：世界坐标 ↔ 像素坐标映射（gfx::View）

> **用途**：记录"世界坐标如何映射到像素坐标"的机制决策：能力落位、2D 映射模型、矩阵/函数/变换链三条入口的一致性、fit（contain）语义与参数守卫、视口原点、3D 观察/投影与线段绘制（近平面裁剪）、投影参数与换算、NDC 深度、两阶段分解、相机基/世界矩阵/反投影。
> **目标读者**：使用或扩展相机/视图能力（拾取、多视口、3D 管线）的人与 AI Agent。
> **关联文档**：[adr-0001](adr-0001-gfx-math-canvas.md)（Canvas/Window）；[adr-0004](adr-0004-transform-chain.md)（变换链/几何层）；[adr-0005](adr-0005-raster-polygon-fill.md)（光栅化，View 的像素坐标下游）；实现 [view.hpp](../../src/include/gfx/render/view.hpp)；教学主题 [view-projection](../../tutorials/view-projection/README.md)、[projection-3d](../../tutorials/projection-3d/README.md)。

> **状态**：✅ 采纳（2026-09-16）。本 ADR 落地：`gfx::View`（2D 正交映射 + fit + 视口原点 + Mat3/变换链；3D lookAt / 透视 / 正交投影 / 近平面裁剪 / `projectSegment` / `drawLine3D`）与测试 `test_view`；示例主题 [projection-3d](../../tutorials/projection-3d/README.md)。同日再补（D8/D9）：视体参数查询与换算、NDC 深度、投影两阶段分解、相机基/位置、相机→世界矩阵、ModelView、反投影射线。

## 背景

- 已有能力：`Canvas`（像素画布）、`Window`（整帧上屏）、`gfx_math`（Vec/Mat/变换链/几何层）、`raster`（画线 + 扫描线填充）。**缺少"世界坐标 → 像素坐标"的公共抽象**。
- 触发现状：这段映射在演示程序里被**手写**过（[polygon_fill.cpp](../../tutorials/polygon-scan/code/polygon_fill.cpp) 的 `toPixelSpace`：包围盒等比缩放 + 居中 + y 翻转）。它是通用需求（任何"画自己的坐标数据"的程序都要做），却散落在应用层，无法复用、无法反向（拾取）、也没被单独测试。
- 项目约束：零第三方依赖；`src/` 是权威实现，`tutorials/` 是教学演示；新增公共 API 须配 ADR 与头文件注释（[api-docs.md](../conventions/api-docs.md)）。
- 坐标系现状：`Canvas` 用像素坐标（y 向下）；几何层/数学层用**世界坐标**（y 向上，右手系）；二者之间的翻转此前无统一约定。

## 决策

### D1：能力进 `src/`，成为 `gfx` 公共 API（`include/gfx/render/view.hpp`）

- 理由：坐标映射是通用能力（被任意绘制程序消费），与 `Canvas`/`raster` 同级；且它必须能被**离屏测试**（不依赖窗口）。
- 类名 `View`（相机/视图一体）：既表达"用哪台相机看世界"，也表达"这段世界范围映射到哪块视口"。
- **不**改 `Canvas` / `raster` 的坐标语义：`View` 只负责产出像素坐标，落像素的取整与采样规则仍归 [raster.hpp](../../src/include/gfx/render/raster.hpp)（避免两处规则漂移）。

### D2：2D 模型 = 正交 + **等比**缩放 + 单一观察中心

状态只有三项：视口像素尺寸（`viewportW/H`）、观察中心（`center`，世界坐标）、缩放（`scale`，1 世界单位 = 多少像素）。

- **等比**（x/y 同一个 `scale`）⇒ 不产生非等比拉伸；世界矩形与视口纵横比不一致时，较松的一维留白（contain）。
- 需要非等比映射（拉伸到满视口）时，由调用方自行组合 `Mat3`，不为此增加状态与分支。
- 映射公式（`halfW`/`halfH` = 视口宽/高的一半）：
  - 世界 → 像素：`px = halfW + (wx − cx)·s`，`py = halfH − (wy − cy)·s`
  - 像素 → 世界：`wx = cx + (px − halfW)/s`，`wy = cy − (py − halfH)/s`

### D3：三条入口 = 同一公式（函数式 / 矩阵 / 变换链）

| 入口 | 形态 | 用途 |
|------|------|------|
| `worldToPixel` / `pixelToWorld` | 直接函数（含数组批量重载） | 直白用法、逐点换算 |
| `matrix()` / `inverseMatrix()` | 行主序 `Mat3`（含 y 翻转的仿射项） | 需要矩阵时（自定义管线、GPU 预留） |
| `asTransform()` | `math::Transform<3>` | 挂到 `Point2D`/`PointBatch2D` 的变换链复用（ADR-0004） |

- 三者必须是**同一公式的不同包装**（测试逐点对照），否则"改一处漏一处"必然漂移。
- 世界→像素矩阵（行主序）：
  ```
  [ s,   0,  halfW − cx·s ]
  [ 0,  −s,  halfH + cy·s ]
  [ 0,   0,  1            ]
  ```

### D4：fit 是 contain，参数非法一律"忽略本次设置"

- `fitWorld(min, max, marginPx)`：把世界矩形**完整装入**视口（`s = min(availW/spanX, availH/spanY)`），并把视口中心对准矩形中心；`marginPx` 是四边留白。
- `fitPoints(points, marginPx)`：取包围盒后走 `fitWorld`；**空集不改动视图**。
- 退化处理：宽或高为 0（线段）时用另一维定缩放；两者皆 0（单点）时只对准中心、不改缩放。
- 参数守卫：`setViewport`（非正）、`setScale`/`zoomBy`（非正）**忽略本次调用**，保持原值 —— 与 `math::normalize`（零向量保持零向量）同风格：出错不抛异常、不产生 NaN。理由：链式配置 API 适合"容错跳过"，硬失败会打断调用方的表达。
- `visibleWorldMin/Max`：当前可见范围的世界包围盒（拾取、剔除的前置）。

### D5：3D = 观察/投影数学 + **可绘制链路**（近平面裁剪是前提）

- `lookAt(eye, target, up)`：右手系观察矩阵（相机看向 −z）。
- `setPerspective(fovYRadians, aspect, zNear, zFar)` / `setOrthographic(l, r, b, t, zNear, zFar)`：右手系，NDC z ∈ [−1, 1]（与 OpenGL 惯例一致）。
- `projectToPixel(Vec3)`：`projection × view` → 透视除法 → NDC → 视口像素（含 y 翻转）。**不做裁剪**（单点无从裁起，相机后方的点结果无定义）。
- **`projectSegment(a, b, outA, outB)`（2026-09-16 补充）**：线段投影，**含近平面裁剪** —— 先在裁剪空间把端点裁到 `w = zNear`（近平面，NDC 深度 −1）再做除法；整段在相机后方则返回 false。
- **`drawLine3D(canvas, a, b, color)`（2026-09-16 补充）**：`projectSegment` + `gfx::drawLine` 的**浮点端点版**（投影出的像素坐标几乎总带小数，用整数版会丢亚像素精度）。
- 退化与非法**保持原矩阵不变**：`eye == target`（无视线方向）、`up` 与视线平行（叉积退化）、`fovY ∉ (0, π)`、`aspect ≤ 0`、`zNear ≤ 0`、`zFar ≤ zNear`、`right ≤ left` 等。
- 边界：仍**没有 3D 光栅管线**（三角形填充/深度缓冲），故只支持**线框**、不做隐藏线消隐；只有近平面裁剪（远平面/视锥侧面未裁）。

> **为什么近平面裁剪是硬需求**：透视下相机后方的点齐次 `w < 0`，直接 `x/w` 会把它**镜像翻到画面内**（"鬼影线"）。实测把相机放进立方体内部（eyeDist 1.2）时，"不裁剪"会在画面内多画 816 个像素、其中 6 条线段端点被除到极远；裁剪后输出与"不裁剪结果"相差 31 像素 —— 差异即裁剪挽回的错误绘制。

### D6：视口是**矩形**（含原点），不只是一对宽高

- `setViewportOrigin(x, y)`（2026-09-16 补充）：视口左上角在画布中的像素位置，默认 (0, 0)。
- `worldToPixel` / `pixelToWorld` / `matrix()` / `inverseMatrix()` / `clipToPixel` 全部含该偏移（测试锁定"三条入口仍同一公式"）；`visibleWorldMin/Max` 不受影响（只取决于观察中心与缩放）。
- 动机：分屏/多视口是刚需（`projection-3d` 示例就是左右两块视口各持一个 View），而"View 描述一块视口矩形"本是它的职责 —— 原先隐含原点 (0,0) 属于能力缺失，不是简化。

### D7：y 轴翻转只发生在这里一次

- 约定：世界坐标 y 向上（数学/几何层），像素坐标 y 向下（Canvas）。`View` 是**唯一**承担翻转的地方（`matrix()` 的 `−s` 项）。
- 因此下游（`raster`/`Canvas`）不需要知道世界坐标的存在，继续只按像素坐标工作。

### D8：投影参数要**留下来**（可查询、可换算），并给出深度与两阶段分解

- **参数记录**：`setOrthographic` / `setPerspective` 不再"算完就丢"，而是把参数存入 `ViewFrustum`（`left/right/bottom/top/nearZ/farZ`）与 `kind_`（`ProjectionKind`）。
  - 深度用**正的距离**（`nearZ` = 近平面到相机的距离）；讲义里用的是视空间 z 坐标（负值，故 `n > f`），注释中写明了这一换算。
- **查询**：`projectionKind()` / `frustum()` / `fovYRadians()` / `aspect()`。
  - 动机：没有这些，视锥剔除、调试显示、"当前 FOV 是多少"都无从下手。
- **换算**：`frustumOfPerspective(fovY, aspect, n, f)` 与 `perspectiveOfFrustum(frustum, &fovY, &aspect)` 互为逆。
  - `top = nearZ·tan(fovY/2)`、`right = top·aspect` ⇒ `fovY`/`aspect` 与 `l/r/b/t` 是**同一视体的两种参数化**（讲义 p46 与 p51 讲的是同一件事）。实用场景：让透视与正交"视野对齐"做对比渲染。
- **深度**：`projectToNdc(world, out)` 输出 `(x, y, z)_ndc`；`z_ndc` 是深度测试/排序所需的量（当前只画线框，不做深度缓冲）。相机后方（`w ≤ 0`）返回 `false`，与 `projectSegment` 的可见性口径一致。
- **两阶段分解**：`projectionDecomposition()` 返回 `{perspToOrtho, ortho}`，满足 `projectionMatrix() == ortho · perspToOrtho`（讲义 p50-56 的"Mortho · Mpersp→ortho"）。正交时 `perspToOrtho = I`，分解恒成立（语义自洽）。
  - 测试锁定：分解乘积逐元素等于 `projectionMatrix()`，且 `ortho` 等于用**近平面矩形**调 `setOrthographic` 得到的矩阵。

### D9：相机基、相机→世界矩阵、ModelView 与反投影

- **相机基与位置**：`cameraRight/cameraUp/cameraBack`（观察矩阵 `V` 的前三行）与 `eyePosition()`（由 `V = [R | −R·e]` 且 `R` 正交 ⇒ `e = −Rᵀ·t`，不必求逆）。此前 `lookAt` 只存矩阵，外界拿不到这几个量。
- **`cameraToWorldMatrix()` = `C = V⁻¹`**（讲义 p37-38）：直接用 `transform3d::frame(右轴, 上轴, 后轴, 相机位置)` 构造（见 [adr-0007](adr-0007-math-transform-completion.md) D4 —— `C` 与 `frame` 是同一件事，避免两处实现）；与通用逆 `Mat4::inverse` 逐元素互校。
- **`modelViewMatrix(M)` = `V·M`**（讲义 p39）：把模型空间直接送到相机空间。
- **`pixelToRay(px, py, origin, dir)`**：屏幕像素反投影为**世界射线**（3D 拾取），内部走"像素 → NDC → 投影矩阵的逆 → 相机空间 → 观察矩阵的逆 → 世界"，并用 `fromHomogeneous` 做齐次除法；与 `projectToPixel` 构成往返（测试锁定）。

## 后果

- 新增公共 API：`gfx::View`（`setViewport`/`setViewportOrigin`/`setCenter`/`setScale`/`zoomBy`/`fitWorld`/`fitPoints`/`worldToPixel`/`pixelToWorld`/`matrix`/`inverseMatrix`/`asTransform`/`visibleWorldMin`/`visibleWorldMax`/`lookAt`/`setPerspective`/`setOrthographic`/`viewMatrix`/`projectionMatrix`/`viewProjectionMatrix`/`projectToPixel`/`projectSegment`/`drawLine3D`）。
- 新增公共 API（D8/D9 补充）：`ProjectionKind`、`ViewFrustum`（含 `width()/height()`）、`projectionKind()`/`frustum()`/`fovYRadians()`/`aspect()`、`frustumOfPerspective()`/`perspectiveOfFrustum()`、`projectToNdc()`、`ProjectionDecomposition`/`projectionDecomposition()`、`cameraRight()`/`cameraUp()`/`cameraBack()`/`eyePosition()`、`cameraToWorldMatrix()`、`modelViewMatrix()`、`pixelToRay()`。
- 新增测试 `test_view`（2D 映射、fit、视口原点、3D 手算值与**近平面裁剪**、`drawLine3D`、视体查询与换算、NDC 深度、投影分解、相机基/位置/C/ModelView/相对运动不变性、反投影射线往返）；新增教学主题 [projection-3d](../../tutorials/projection-3d/README.md) 与演示目标 `tutorial_projection_3d`（产物 `Projection3D`）。
- `view.hpp` 依赖 `raster.hpp`（`drawLine3D` 调用浮点端点版 `drawLine`）；反向无依赖 ⇒ 无循环。头文件实现、零依赖、无平台相关代码；`src/CMakeLists.txt` 仅登记头文件（不新增编译单元）。
- 演示程序 `polygon_fill.cpp` 的手写 `toPixelSpace` 已被 `View::setViewport + fitPoints + worldToPixel` 替代（新旧实现输出图**逐像素 0 差异**，见"验证"）。

## 验证（可复现）

- 单元测试 `ctest -R test_view`（350 checks）：
  - 2D 基本映射与往返（含平移 + 缩放后）；批量接口与逐点一致；`setViewport(Canvas)` 重载；
  - **三条入口同一公式**：`matrix()`、`inverseMatrix()`、`asTransform()` 分别与 `worldToPixel`/`pixelToWorld` 逐点对照；`inverse · matrix ≈ I`；
  - fit：contain 的 scale/中心/四角像素值（800×600 视口 + 100×100 世界方框 ⇒ scale 6；留白 24 ⇒ scale 5.52 且四边恰好各余 24px）；宽扁矩形等比不拉伸；线段/单点/空集退化；
  - 可见包围盒与 `pixelToWorld` 角落一致；
  - **与历史手写映射对照**：同一组顶点经 `fitPoints` + `worldToPixel` 与旧 `toPixelSpace` 公式结果逐点一致（1e-3）；
  - 3D：`lookAt` 矩阵手算值（eye 在 (0,0,5) 看原点 ⇒ 世界原点映射到相机空间 (0,0,−5)）；透视矩阵手算值 + 近/远平面 → NDC z = ∓1；`projectToPixel` 的视口中心与右/上边界；正交投影近/远平面 → ∓1；退化与非法参数保持原矩阵。
  - **替换回归**：`polygon_fill.cpp` 改用 `View` 后，480×360 与 800×600 两组结果图与改动前逐像素比较 **0 差异**（自写 PNG 解码器逐块 CRC + 逐像素比对）；作业提交包重建后独立构建 + `ctest` 通过。
  - **视口原点**（2026-09-16 补充）：同一世界点在左右两块视口相差一个视口宽度；往返成立；矩阵/变换链仍与函数式一致；可见世界范围不受原点影响。
  - **近平面裁剪**（2026-09-16 补充）：全可见线段与 `projectToPixel` 逐点一致；整段在相机后方返回 false；一端在后方时裁到近平面后落在**正确一侧**（`(0,0,0)→(10,0,10)` 裁后像素 x = 2000 > 画面宽；不裁剪会镜像到 x ≈ 80 的反侧）；正交投影 `w ≡ 1` 不触发裁剪。
  - **`drawLine3D`**（2026-09-16 补充）：端点落在预期像素；整段在相机后方时**一个像素都不画**（否则即"鬼影"）。
  - **视体参数与换算**（D8）：透视 `fovY=90°、aspect=2、nearZ=1` ⇒ `top=1、right=2`；两个换算函数互逆；正交原样记录视体且 `aspect = 宽/高`；退化视体反换算返回 false。
  - **NDC 深度**（D8）：近/远平面 ⇒ `z_ndc = ∓1`；相机前 5 单位 ⇒ `z_ndc ≈ 0.616`；相机后方返回 false。
  - **投影两阶段分解**（D8）：`ortho · perspToOrtho` 逐元素等于 `projectionMatrix()`；`ortho` 等于用**近平面矩形**调 `setOrthographic` 的矩阵；正交时压缩矩阵为单位阵。
  - **相机与反投影**（D9）：相机基/位置取值正确；`cameraToWorldMatrix` 与 `Mat4::inverse` 逐元素一致且第 4 列 = 相机位置；ModelView == "先 M 后 V" 两步；**相对运动不变性** `V(G·相机)·G == V(相机)`（相机与物体同乘刚体变换 `G`，观察结果不变）；`pixelToRay` 中心像素方向 = 相机前方、右/上像素偏移方向正确、并与 `projectToPixel` 构成往返。
- 端到端独立校验（`projection-3d` 示例）：`python artifacts/projection-3d-check/verify_projection.py <png> [eyeDist]` —— 该脚本**自己实现**矩阵与光栅化后逐像素比对：正常场景 **0 像素差异**，正交棱长比 1.0000（严格等长）/ 透视 1.3888（近大远小）；相机置于立方体内部（eyeDist 1.2）时差异 4 像素（C++ `float` vs 脚本 `double`），且"不裁剪"对照在画面内多出 816 像素、输出与无裁剪结果差 31 像素 ⇒ 裁剪确实生效。

## 待细化

- **参数式 vs 矩阵式的进一步收敛**：若未来出现非等比/旋转相机需求，考虑增加 `setWorldRect`（独立轴）或接受 `Mat3` 的构造入口。
- **视锥剔除**：`pixelToRay` 已提供 3D 拾取的反向路径；但"用 `frustum()` 做正向剔除"（快速排除视体外的物体/线段）尚未实现。
- **3D 管线**：接入 `Canvas` 的深度缓冲/透视校正光栅化（另开 ADR）。
- **版本**：本次为兼容性新增公共 API（不破坏既有调用），经确认**维持 0.1.0**（教学阶段累积到功能性发布时再升 minor）。
