# view-projection —— 相机与视图：把世界坐标搬进像素

> **用途**：讲清"世界坐标（y 向上）如何映射到像素坐标（y 向下）"，以及 `gfx::View` 为什么这样设计。
> **目标读者**：需要把自有坐标数据画成图像的人；学习"视图变换 + 视口变换"的读者。
> **关联文档**：权威实现 [src/include/gfx/render/view.hpp](../../src/include/gfx/render/view.hpp)；设计决策 [adr-0006](../../docs/design/adr-0006-camera-view.md)；下游光栅 [polygon-scan](../polygon-scan/README.md)（[raster.hpp](../../src/include/gfx/render/raster.hpp)）；数学地基 [math-foundations](../math-foundations/README.md)。

> 状态：生效中（2026-09-16）。权威实现见 `src/include/gfx/render/view.hpp`（本页只讲用法与原理，签名以头文件注释为准）。

## 1. 为什么需要它

`Canvas` 只认**像素坐标**：`putPixel(x, y)` 的 x 向右、y 向下，原点在左上角。

但我们要画的数据几乎从不用像素坐标描述：多边形的顶点是"米""格""世界单位"，y 向上（数学惯例）。把这两者对接，是每个绘图程序都要做的第一件事：

```
世界坐标 ——[视图/投影]——> 像素坐标 ——[raster]——> Canvas 像素
   (你算出来的)              (屏幕上的)
```

这段映射起初是**手写**在演示程序里的（`polygon_fill.cpp` 曾有一个 `toPixelSpace`）。它有三个问题：不能复用、不能反向（像素 → 世界，做拾取要用）、没法单独测试。`View` 把这段独立成可按需配置、可复用的对象；`polygon_fill.cpp` 现已改用它（新旧实现输出图逐像素一致）。

## 2. 两段变换，一个对象

教科书把这件事拆成两段：

| 段 | 职责 | 在 2D 里就是 |
|----|------|-------------|
| 视图变换（view） | 世界 → 相机/眼睛空间：决定"相机在哪、看多大范围" | 平移（对准观察中心）+ 缩放 |
| 视口变换（viewport） | 相机空间 → 像素：决定"画面占哪块像素" | 平移到视口中心 + **y 轴翻转** |

2D 正交场景下这两段都是简单的仿射变换，`View` 把它们**合并**成一个对象，状态只有三项：

- `viewportWidth/Height`：像素视口尺寸（通常就是 `Canvas` 的宽高）；
- `center`：视口中心对准的**世界**坐标点；
- `scale`：1 个世界单位等于多少**像素**。

## 3. 映射公式（这就是全部）

记 `halfW = 视口宽/2`、`halfH = 视口高/2`、`s = scale`、`c = center`：

```
世界 → 像素：  px = halfW + (wx − c.x) · s
              py = halfH − (wy − c.y) · s     ← 减号就是 y 轴翻转

像素 → 世界：  wx = c.x + (px − halfW) / s
              wy = c.y − (py − halfH) / s
```

写成矩阵（行主序 `Mat3`，作用方式是 `M * Vec3(wx, wy, 1)`）：

```
[ s,   0,  halfW − c.x·s ]
[ 0,  −s,  halfH + c.y·s ]
[ 0,   0,  1            ]
```

即 [`View::matrix()`](../../src/include/gfx/render/view.hpp)。注意内存里是 `data[row*3 + col]`，别按列主序读（见 [mat.hpp](../../src/include/gfx/math/mat.hpp)）。

> **为什么要翻转 y**：世界坐标 y 向上（数学习惯，正角是逆时针），像素坐标 y 向下（屏幕习惯，行号从上往下）。翻转集中在 `View` 一处，下游的 `raster`/`Canvas` 就不必知道"世界坐标"的存在。

## 4. fit：让数据"完整装入"画面

实际用的最多的不是手工设 `center`/`scale`，而是"给我把这一堆点装进画面"：

```cpp
gfx::View view;
view.setViewport(canvas);                 // 视口 = 画布尺寸
view.fitPoints(worldVertices, 24.0f);     // 顶点集等比装入，四边留 24px
```

`fitPoints` 先取包围盒，再走 `fitWorld`：**等比**缩放（`s = min(可用宽/世界宽, 可用高/世界高)`），并把视口中心对准包围盒中心。

- **等比**是关键：x/y 用同一个 `s`，图形不会被拉扁。代价是较松的一维会多留白（contain 语义）。
- 想自己控制范围就调 `fitWorld(Vec2 min, Vec2 max, marginPx)`；想直接指定就 `setCenter` + `setScale`。
- 退化输入不会崩：线段（宽或高为 0）用另一维定缩放；单点只对准中心、不改缩放；空点集不改动视图。

## 5. 用起来（完整示例）

```cpp
#include <gfx/core/canvas.hpp>
#include <gfx/render/png.hpp>
#include <gfx/render/raster.hpp>
#include <gfx/render/view.hpp>

#include <vector>

int main() {
    gfx::Canvas canvas(800, 600);
    canvas.clear(gfx::rgb(16, 24, 48));

    // 世界坐标顶点（y 向上）
    const std::vector<gfx::math::Vec2> world{
        {100.0f, 80.0f}, {220.0f, 80.0f}, {260.0f, 180.0f},
        {160.0f, 260.0f}, {60.0f, 180.0f}};

    // 相机：视口 = 画布；顶点集等比装入，四边留 24px
    gfx::View view;
    view.setViewport(canvas).fitPoints(world, 24.0f);

    // 世界 → 像素（批量，逐点等价）
    const std::vector<gfx::math::Vec2> pixels = view.worldToPixel(world);

    // 交给光栅层：先填充、后描边（边界覆盖在填充之上，见 ADR-0005 D2）
    gfx::fillPolygon(canvas, pixels, gfx::rgb(70, 130, 200));
    gfx::drawPolygonOutline(canvas, pixels, gfx::rgb(250, 220, 120));

    gfx::savePng(canvas, "output.png");
}
```

反向也要用得上（例如鼠标点选）：`view.pixelToWorld(mousePixel)` 把像素点换回世界坐标；`view.visibleWorldMin()/Max()` 给出当前看得到的世界范围。

## 6. 浮点像素坐标怎么落笔（与 raster 的衔接）

`View` 产出的是**浮点**像素坐标，不要自己先 `lround` —— 取整丢掉的亚像素信息，造成的偏差远超"半个像素"：

- **填充**：`fillPolygon` 内部按**像素中心采样**（`i+0.5`），直接传浮点坐标即可。
- **画线**：用 `drawLine`（坐标平铺为 `float`，可带小数），它不提前取整、逐点取最近像素。若先 `lround` 再走整数版，误差项会从取整后的端点**重算**，导致阶梯**整体错位**：实测 `(0.0,0.4) → (100.0,10.4)` 与"不取整"相比差 **56.7%** 的像素。
- **`Point2D` 需先求值**：`Point2D` 经隐式转换（或 `.value()`）取**变换后**的坐标，再以平铺坐标传入，例如 `const math::Vec2 pv = p; drawLine(canvas, pv.x, pv.y, qv.x, qv.y, color);`（见 [point.hpp](../../src/include/gfx/geom/point.hpp)）。
- **`drawPolygonOutline`** 目前内部仍是"先 `lround` + 整数 Bresenham"（作视觉边界够用）；要更精确的轮廓，逐边调带小数坐标的 `drawLine`。
- 想要**抗锯齿**（而不是硬边最近像素），是另一条路线（覆盖率着色 + alpha 混合），见 [todo.md](../../docs/todo.md)。

## 7. 3D：观察、投影与线框绘制

`View` 也提供完整的 3D 链路（**仍无线框以外的 3D 光栅：没有三角形填充与深度缓冲**，见 [projection-3d](../projection-3d/README.md)）：

```cpp
gfx::View camera;
camera.setViewport(800, 600);
camera.lookAt(gfx::math::Vec3(0.0f, 0.0f, 5.0f),      // 相机位置 eye
              gfx::math::Vec3(0.0f, 0.0f, 0.0f),      // 看向 target
              gfx::math::Vec3(0.0f, 1.0f, 0.0f));     // 上方向 up

// 垂直视场角用**弧度**（90° = π/2 ≈ 1.5708），与 gfx_math 的 rotate 口径一致
camera.setPerspective(1.5708f, 800.0f / 600.0f, 0.1f, 100.0f);

// 单点投影（不裁剪：点在相机后方时无定义）
const gfx::math::Vec2 px = camera.projectToPixel(gfx::math::Vec3(5.0f, 0.0f, 0.0f));

// 线段投影 / 绘制：**含近平面裁剪**
gfx::math::Vec2 a, b;
if (camera.projectSegment(p0, p1, a, b)) gfx::drawLine(canvas, a.x, a.y, b.x, b.y, color);
camera.drawLine3D(canvas, p0, p1, color);             // 等价的一步写法
```

约定：右手系、相机看向 −z、NDC 的 z ∈ [−1, 1]（与 OpenGL 惯例一致）。退化输入（`eye == target`、`up` 与视线平行）与非法参数（`fovY` 不在 (0, π)、`zNear ≤ 0`、`zFar ≤ zNear` …）都**保持原矩阵不变**，不会写出 NaN。

> **为什么画 3D 线段必须裁剪**：相机后方的点齐次 `w < 0`，直接 `x/w` 会把它镜像翻到画面里（鬼影）。`projectSegment` 先把端点裁到近平面再除法。完整的对比例子见 [projection-3d](../projection-3d/README.md)。

分屏/多视口用 `setViewportOrigin(x, y)`：同一画布上并排两块视口，各持一个 `View` 即可（正交 vs 透视对比就是这么画的）。

## 8. 常见误区

- **把世界坐标直接丢给 `raster`**：`raster` 收的是像素坐标。先 `worldToPixel`（或 `asTransform()` 挂到 `Point2D` 上）再画。
- **以为 `scale` 可以两轴不同**：`View` 只有一个 `scale`（等比）。要非等比拉伸，自行组合 `Mat3`。
- **在别处再翻一次 y**：翻转只在 `View` 内发生一次；调用方自己再翻就上下颠倒了。
- **给 `setPerspective` 传角度**：它收**弧度**，与 `gfx_math::transform2d/3d::rotate` 保持同一口径。
- **参数非法时期待报错**：链式配置 API 的策略是"忽略本次设置"（保持原值），不抛异常（见 [adr-0006](../../docs/design/adr-0006-camera-view.md) D4）。

## 9. 权威实现与自测

- 权威实现：[src/include/gfx/render/view.hpp](../../src/include/gfx/render/view.hpp)（签名与语义以头文件注释为准）。
- 设计决策：[adr-0006](../../docs/design/adr-0006-camera-view.md)。
- 验证：`ctest --test-dir build -C Release -R test_view`（210 checks：映射往返、三条入口同一公式、fit 语义、视口原点、与教程手写映射对照、3D 手算值与**近平面裁剪**、`drawLine3D`）。
