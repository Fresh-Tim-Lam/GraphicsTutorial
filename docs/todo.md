# todo.md — 活跃待办

> **用途**：记录当前活跃、未关闭的工作项（项目级 TODO）。事项完成即移除；跨会话承接靠 [sessions/](sessions/README.md)。
> **目标读者**：仓库维护者 / AI Agent（每次开工前可扫一眼）。
> **关联文档**：[docs/README.md](README.md)（docs 总索引）。

> 状态：生效中（核验于 2026-09-09）。

## 已完成（近期）

- [x] 首个模块 0.1.0 落地：gfx_math 头文件子库 + Canvas 帧缓冲 + Window（GDI DIB/BitBlt）上屏；测试 2 项通过；教学树首批主题（pixels-on-screen、math-foundations）+ 打包脚本可用。
- [x] ADR-0002 全模板化：gfx_math 改为 Mat&lt;N&gt;/Vec&lt;N&gt; 模板 + mat2/3/4、vec2/3/4 别名；算法收敛 math::detail（为未来 CPU/GPU 优化留插入点）。测试与演示保持通过。
- [x] ADR-0003 GPU 预留分派点：新增 batch.hpp 批量变换接口 + 运行时后端钩子（CPU 默认，可注入自定义/CUDA 后端）；test_batch 通过。
- [x] 变换矩阵辅助（transform.hpp）：transform2d/transform3d 的平移、缩放、旋转（含任意轴）、翻转；test_transform 通过，教学 demo 改用它。
- [x] ADR-0004 变换链机制：`math::Transform<N>`（记录/折叠/apply）+ `gfx::Point2D/Point3D`、`PointBatch2D/PointBatch3D`（点 apply 变换、视图零拷贝含步长）；test_chain、test_point 通过。
- [x] **画线/图元绘制 + 扫描线多边形填充**（作业 1）：`gfx::drawLine`（Bresenham）+ `ScanlineFiller`/`fillPolygon`（边表 ET + 活性边表 AET、半开区间规则）+ 自写 `savePng`；教学主题 [tutorials/polygon-scan/](tutorials/polygon-scan/README.md) 与程序 `PolygonFill`（文本/交互输入、窗口扫描线动画、PNG 输出）；test_raster、test_png 通过（ADR-0005）。
- [x] 逐项文件组织自检 & 首次正式 commit + 发布：提交 `3ec62e0`、tag `v0.1.0`；发布包 `packaging/release/`（include + 导入库 + DLL）已用"只用发布产物"的消费者程序编译链接并运行验证；仓库已推送至 GitHub（公开）。
- [x] **相机/视图公共 API**：`gfx::View` —— 世界坐标 ↔ 像素坐标（2D 正交等比 + 观察中心 + fit/contain；函数 / Mat3 / 变换链三入口同一公式；3D 预留 lookAt 与透视/正交投影 + projectToPixel）；教学主题 [tutorials/view-projection/](tutorials/view-projection/README.md)；test_view 通过（ADR-0006）。
- [x] **演示程序改用 `View`**：`polygon_fill.cpp` 删除手写 `toPixelSpace`，改为 `View::setViewport + fitPoints + worldToPixel`；新旧实现输出图**逐像素 0 差异**（480×360 与 800×600 两组）；作业提交包已重建（ADR-0006）。
- [x] **3D 投影链路与浮点端点画线**：`drawLine(Vec2,Vec2)` 浮点端点入口（整数端点下与 Bresenham 逐像素一致）；`View` 加 `projectSegment`（**近平面裁剪**，防相机后方点的"镜像鬼影"）与 `drawLine3D`，`setViewportOrigin` 支持分屏；教学主题 [tutorials/projection-3d/](tutorials/projection-3d/README.md)（正交 vs 透视同框对比 + 近距裁剪演示）；test_raster 86 / test_view 210 checks；示例输出经独立脚本逐像素校验（0 差异）（ADR-0005/0006）。
- [x] **第三讲（变换、相机与投影）知识点落地**：`gfx_math` 补错切 `shear`、逆变换闭式、向量变换 `applyVector`（w=0）、齐次归一化 `fromHomogeneous`、欧拉角 `euler`、带原点坐标基 `frame`、仿射拆解 `translationOf`/`linearPartOf`/`isAffine`（ADR-0007，test_transform 427 checks）；`View` 补视体参数查询与 `fov↔l/r/b/t` 换算、NDC 深度、投影两阶段分解、相机基/位置、相机→世界矩阵、ModelView、反投影射线 `pixelToRay` 与相对运动不变性测试（ADR-0006 D8/D9，test_view 350 checks）。
- [x] **第五讲（光栅化、采样与抗锯齿）知识点落地**：新增 `triangle.hpp`（边函数 + **top-left 边界规则**的 `insideTriangle`、`barycentric` 重心坐标、包围盒版 `fillTriangle`、增量式 `TriangleFiller`/`fillTriangleScanline`、`coverage` + `fillTriangleAA` 覆盖率抗锯齿 + `fillTriangleSSAA` 超采样）与 `depth.hpp`（`DepthBuffer` + `fillTriangleDepth`）；`Color`/`Canvas` 补 alpha 基础（`rgba`/`blend`/`blendPixel`）；教学主题 [tutorials/triangle-raster/](tutorials/triangle-raster/README.md) 与程序 `TriangleRaster`（三种采样方式对比 + 乱序深度测试）；test_triangle 55 checks（ADR-0008）。
- [x] **作业二（欧拉操作）**：新增 `halfedge.hpp`（半边/B-rep：`HalfEdgeMesh` + MVFS/MEV/MEF/KEMR/KFMRH + `validate`/`counts`/`eulerCharacteristic`/`topologyText`）；教学主题 [tutorials/euler-ops/](tutorials/euler-ops/README.md) 与程序 `EulerOps`（四面体连续构造 + KEMR 内环 + KFMRH 内环，逐步校验并渲染线框）；test_halfedge 79 checks（ADR-0009）；作业提交包见 `artifacts/homework2/`。

## 待办

- [ ] **真实 CUDA 后端**（独立于本库，需 nvcc/GPU 工具链；接入时调用 setBatchTransformBackend）——ADR-0003 待细化。
- [ ] **大型三维模型存取与顶点缓冲**：连续顶点数组（M×4 布局）+ 加载 → 整批 batchTransform（对应 ADR-0003 补充记录；属模型数据层）。
- [ ] **多轮廓/带孔多边形填充**（奇偶规则 vs 非零绕组规则）与边界抗锯齿（覆盖率着色，需 Canvas 支持 alpha 写入）——ADR-0005 待细化（作业 1 提高题）。
- [ ] **拾取 / 3D 光栅管线**：基于 `pixelToWorld` 与 `visibleWorldMin/Max` 的交互拾取与视锥剔除；接入 Canvas 的 3D 三角形填充 + 深度缓冲（当前仅线框、不做隐藏线消隐）——ADR-0006 待细化。
- [ ] **窗口输入细化**：按键/窗口尺寸变化事件；present 与 resize 处理（当前固定客户区）。
- [ ] 教学树"鼠标交互/动画帧循环"主题（若需要）。
- [ ] （可选）CI：GitHub Actions 跑 `cmake --build` + `ctest`，保证远端每次改动可验证。
