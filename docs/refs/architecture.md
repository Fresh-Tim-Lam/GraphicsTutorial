# architecture.md — 架构总览与数据流

> **用途**：记录 GraphicsTutorial 的整体目录结构（摘要同步自 directory-organization.md，冲突以 directory-organization 为准）与核心数据流；随开发持续补充模块职责。
> **目标读者**：需要系统级理解的开发者 / AI Agent / 读者。
> **关联文档**：[directory-organization.md](../conventions/directory-organization.md)；[src/](../../src/README.md)；[adr-0001](../design/adr-0001-gfx-math-canvas.md)。

> 状态：生效中（核验于 2026-09-09）。首个模块 0.1.0 已落地，数据流已可跑通（Canvas → Window → 屏幕）。

## 总览

- 从 0 造轮子：仅用 Windows 最基础屏幕像素着色能力（GDI DIB + BitBlt），向上构建可调用的 `gfx` 渲染 API。
- 交付形态：动态库（DLL + 导入库）+ 头文件，命名空间 `gfx`；另有独立头文件子库 `gfx_math`（gfx::math）。
- 打包入口：packaging/build_release.cmd。

## 数据流（当前已跑通）

1. （2D 绘制的前置步骤）调用方用 `gfx::View` 把**世界坐标**映射到**像素坐标**：`setViewport(canvas)` + `fitPoints(worldVertices, margin)` → `worldToPixel(...)`；反向可用 `pixelToWorld`。
   3D 线框同理：`lookAt` + `setOrthographic`/`setPerspective` → `drawLine3D`（内部**先做近平面裁剪**，再投影为像素线段并绘制）。
   分屏/多视口：`setViewportOrigin(x, y)` 指定视口左上角，并排多个 `View` 即可。
2. 调用方创建 `gfx::Window(title, w, h)` → 内部建 Canvas（内存帧缓冲）+ Win32 窗口与 DIB。
3. 调用方在 `win.canvas()` 上绘制：`putPixel` 逐像素写字，或用光栅化原语 `drawLine` / `fillPolygon`（内部仍是 `putPixel`）。
4. `win.present()`：Canvas → DIB（32bpp BGRA 转换）→ `BitBlt` 整帧上屏。
5. `win.pollEvents()`：处理窗口消息；返回 false 表示关闭。
6. 需要落盘时：`gfx::savePng(canvas, path)`（自写编码器）把帧缓冲写成 PNG。

> 教学视角：这正是软件光栅化的输出末端——先决定每个像素颜色（离屏），再一次提交到屏幕或文件。

## 模块职责

| 模块 | 职责 | 位置 | 状态 |
|------|------|------|------|
| gfx_math | 模板 Mat&lt;N&gt;/Vec&lt;N&gt;（N=2/3/4）及核心运算（含齐次归一化、向量变换、仿射拆解）；变换原语 transform2d/3d + **变换链 Transform&lt;N&gt;**（含错切/欧拉角/带原点坐标基/逆变换闭式）；批量变换 + 后端钩子；算法收敛 math::detail（头文件子库，零依赖） | src/include/gfx/math/ | 0.1.0 已落地（ADR-0002/0004/0007） |
| Canvas | 内存像素帧缓冲（putPixel/clear/data） | src/include/gfx/core/canvas.hpp | 0.1.0 已落地 |
| 几何层（Point/Batch） | Point2D/3D 与 PointBatch2D/3D：点/点批 apply 变换（视图零拷贝、步长支持） | src/include/gfx/geom/point.hpp | 已落地（ADR-0004） |
| **相机/视图（View）** | 世界坐标 ↔ 像素坐标：2D 正交等比 + 观察中心 + fit/contain + 视口原点（三条入口同一公式：函数/Mat3/变换链）；3D lookAt + 正交/透视投影 + **近平面裁剪** + `drawLine3D` 线框绘制 + 视体参数与换算 + NDC 深度 + 投影两阶段分解 + 相机基/相机→世界/ModelView/反投影射线 | src/include/gfx/render/view.hpp | 已落地（ADR-0006） |
| Color | RGBA8 像素颜色 | src/include/gfx/core/color.hpp | 0.1.0 已落地 |
| Window | Win32 窗口 + DIB/BitBlt 整帧上屏 | src/include/gfx/render/window.hpp + window_win32.cpp | 0.1.0 已落地 |
| **光栅化（Raster）** | 画线（整数 Bresenham / 浮点端点 DDA 两条入口）、多边形轮廓、扫描线填充（边表 ET + 活性边表 AET，半开区间规则）、顶点规范化与校验 | src/include/gfx/render/raster.hpp | 已落地（ADR-0005） |
| **三角形光栅化（Triangle）** | 采样判定（边函数 + **top-left 边界规则**）、重心坐标插值、包围盒 / 增量式逐扫描线填充、抗锯齿（覆盖率着色 / 超采样 SSAA）、alpha 混合（`blendPixel`） | src/include/gfx/render/triangle.hpp | 已落地（ADR-0008） |
| **深度缓冲（Depth）** | 逐像素深度测试（Z-Buffer，`GL_LESS` 语义）+ 重心插值深度的三角形填充（顺序无关的遮挡） | src/include/gfx/render/depth.hpp | 已落地（ADR-0008） |
| **半边结构（B-rep）** | 拓扑数据：体→面→环→半边→顶点 + 无向边；五个欧拉操作（MVFS / MEV / MEF / KEMR / KFMRH）+ 结构一致性校验（配对、前驱后继、环闭合、归属） | src/include/gfx/geom/halfedge.hpp | 已落地（ADR-0009） |
| **图像输出（PNG）** | 自写 PNG 编码（零依赖：CRC32/Adler32 + zlib 存储块） | src/include/gfx/render/png.hpp | 已落地（ADR-0005） |
| version | 版本唯一事实源 | src/include/gfx/core/version.hpp | 0.1.0 已落地 |
| （规划） | 多轮廓/孔洞填充、MSAA/解析覆盖率、透视校正插值、3D 三角形管线、窗口事件细化等 | — | 见 todo.md |
