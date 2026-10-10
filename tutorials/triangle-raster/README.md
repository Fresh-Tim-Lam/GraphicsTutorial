# triangle-raster —— 三角形光栅化、采样与抗锯齿

> **用途**：讲清"三角形怎么变成像素"（采样判定 + 边界规则 + 重心坐标），以及"锯齿从哪来、抗锯齿怎么做"（覆盖率 / 超采样）；并演示深度缓冲如何解决遮挡。
> **目标读者**：要画填充面片的人；学习采样与抗锯齿的读者。
> **关联文档**：权威实现 [triangle.hpp](../../src/include/gfx/render/triangle.hpp)、[depth.hpp](../../src/include/gfx/render/depth.hpp)；设计决策 [adr-0008](../../docs/design/adr-0008-triangle-raster.md)；另一条路线 [polygon-scan](../polygon-scan/README.md)（扫描线填充）；上游 [projection-3d](../projection-3d/README.md)（把 3D 顶点投到屏幕）。

> 状态：生效中（2026-09-16）。权威实现见 `src/include/gfx/render/triangle.hpp`（签名与语义以头文件注释为准）。

## 1. 三角形怎么变成像素：在像素中心"采样"

光栅化 = **对一个二维指示函数采样**（讲义 p33-37）：

```
inside(三角形, x, y) = 1 若点 (x, y) 在三角形内，否则 0
```

采样点取**像素中心** `(x + 0.5, y + 0.5)`——这与 [raster.hpp](../../src/include/gfx/render/raster.hpp) 的扫描线填充用的是同一条约定。

判定"点是否在三角形内"用**三个叉积**（讲义 p40）：

```cpp
float e = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);   // 边 a→b 的边函数
// 三条边的边函数同号 ⇒ 点在三角形内
```

## 2. 边界情况：采样点恰好落在边上算谁的？

讲义 p41 的"边界情况"是**必须解决**的问题：两个三角形共享一条边，落在边上的采样点如果两边都算，就会被填两次（重叠）；都不算，就会漏掉一行像素（裂缝）。

`insideTriangle` 采用 **top-left 规则**：只有 **top 边**与 **left 边**接受边界上的采样点。

```cpp
// 屏幕坐标 y 向下：向下走的边（dy > 0）算 left；水平且向左走的边算 top
inline bool isTopLeftEdge(const math::Vec2& a, const math::Vec2& b) {
    return (b.y - a.y > 0.0f) || (b.y == a.y && b.x - a.x < 0.0f);
}
```

项目里这是**第二次**解决同一个问题：`raster.hpp` 的扫描线填充用"y 方向半开区间 + x 方向左闭右开"达到同样目的。两套规则都保证"不重不漏"，但对同一三角形的**边界像素**归属可以不同（因此单元测试在互校两条路线时特意避开了"采样点恰在边上"的坐标）。

**验证是可判定的**：正方形 `[0,4]²` 被对角线切成两个三角形，则 16 个采样点每个**恰好被覆盖一次**（测试锁定：不重复 0、不遗漏 0）。

## 3. 填充的总接口：一个入口 + 模式枚举（四种模式）

对外的三角形填充**只有一个签名**（与 [raster.hpp](../../src/include/gfx/render/raster.hpp) 的 `drawLine` 同一风格：一个入口 + 枚举 + 默认最优）：

```cpp
enum class TriangleFillMode {
    SolidScanline,   // 增量式逐扫描线 + 点采样（默认，最快）
    SolidBBox,       // 包围盒 + 逐像素 inside test + 点采样（最直白，对照）
    CoverageAA,      // 覆盖率抗锯齿：按像素内覆盖面积 alpha 叠色
    SSAA,            // 超采样：渲染 factor 倍大图后下采样平均
};
bool fillTriangle(canvas, a, b, c, color,
                  TriangleFillMode mode = TriangleFillMode::SolidScanline,
                  int samplesPerAxis = 0,               // 0 = 未指定，按模式默认值 4
                  const Color* background = nullptr);   // nullptr = 未指定，用不透明黑
```

| 模式 | 做法 | 定位 |
|---|---|---|
| **`SolidScanline`**（默认） | 三角形是凸的 ⇒ 每行只需求出**一个连续区间**的左右边界 | 更快；边界口径与 `fillPolygon` 一致 |
| `SolidBBox` | 老老实实对包围盒内每个像素做 inside test | 最直白，好懂，用于对照 |
| `CoverageAA` | 按像素被覆盖的**面积比例**叠色（`blendPixel`） | 抗锯齿；像素可取中间值 |
| `SSAA` | 渲染到 `samplesPerAxis` 倍分辨率的离屏画布，再逐块平均 | 抗锯齿；贴合"多采样再平均" |

分派**只有一处**（`fillTriangle` 内的 switch）；实现都在 `detail`：`detail::fillTriangleBBox` / `detail::fillTriangleScanline` / `detail::fillTriangleCoverageAA` / `detail::fillTriangleSSAA`。`TriangleFiller` 类**保留**（增量式专属）：它提供可 `begin`/`step`/`fillCurrentRow` 的**逐扫描线推进**（动画/调试演示）；包围盒法本质是逐像素测试，不提供 `step` 版本。`TriangleFiller` 与 `raster.hpp` 的 `ScanlineFiller` 是**同一套边界口径**（y 半开、x 左闭右开），所以两者对同一三角形给出**逐像素相同**的结果（测试锁定）。

### 哨兵值：`0` / `nullptr` 表示"未指定"

C++ 默认参数分不清"没传"和"传了默认值"，所以用**非法值**当哨兵：`samplesPerAxis = 0` 表示未指定（按模式默认 4），`background = nullptr` 表示未指定（用不透明黑 `(0,0,0,255)`）。有了哨兵，函数才能判断参数是否被**显式指定**。

### 无关参数：警告并忽略

对当前模式**没有意义**的参数不会被静默吞掉，而是打印**一行**中文警告并忽略（不中断、不影响返回值）：

| 模式 | `samplesPerAxis` | `background` |
|---|---|---|
| `SolidScanline` / `SolidBBox` | ⚠️ 警告并忽略 | ⚠️ 警告并忽略 |
| `CoverageAA` | ✅ 使用 | ⚠️ 警告并忽略 |
| `SSAA` | ✅ 使用（= factor） | ✅ 使用 |

```text
[gfx] fillTriangle: 参数 samplesPerAxis 对填充方式 SolidScanline 无意义，已忽略
```

只在参数被**显式指定**时才警告（`fillTriangle(c, a, b, c, col)` 不传后两个参数 ⇒ 无警告）。警告可用开关关闭：

```cpp
gfx::triangleFillWarningsEnabled() = false;   // 批量渲染/测试时关掉，避免刷屏
```

> ⚠️ 两种**点采样覆盖判定**（`SolidScanline` 半开区间 vs `SolidBBox` top-left）只在"采样点恰好落在边上"处可能不同（top-left 规则 vs 半开区间规则，见 §2）：顶点带小数时逐像素一致；整数顶点、45° 斜边、共享边会相差边界像素。实测差异见 [ADR-0008](../../docs/design/adr-0008-triangle-raster.md) 的 D3。

## 4. 重心坐标：顶点属性怎么插到像素上

```cpp
math::Vec3 w;                       // (wa, wb, wc)，和为 1
barycentric(pixel, a, b, c, w);     // 像素 = wa·a + wb·b + wc·c
float z = w.x * za + w.y * zb + w.z * zc;   // 深度/颜色/法线都能这样插
```

- 顶点处权重为 `(1,0,0)` 等；重心处为 `(1/3, 1/3, 1/3)`；
- 某个权重为 0 ⇒ 点在该边所在直线上；
- 深度测试（下一节）就是靠它做**逐像素深度插值**。

## 5. 锯齿与抗锯齿：先模糊，再采样

讲义的核心结论（p69、p103）：**抗锯齿 = 采样前先滤除高频**。落到实现上有两条常见路线：

| 方式 | 做法 | 本示例中间色像素数 |
|---|---|---|
| 点采样（`fillTriangle`，默认 `SolidScanline`） | 每像素 1 个样本 | **0**（只有纯色或背景） |
| **覆盖率着色**（`fillTriangle(..., CoverageAA, 8)`） | 算像素被覆盖的**面积比例**，按比例混合颜色 | **252** |
| **超采样 SSAA**（`fillTriangle(..., SSAA, 4, &bg)`） | 渲染到 4× 分辨率画布，再逐块平均 | **252** ⓘ |

> ⓘ 本例的斜边是 45°，每个被切到的像素覆盖率恰为 0.5，两种抗锯齿方法因此得到相同中间色数。换个角度（如 30°）两者会有差异——SSAA 是"真的多采样再平均"，覆盖率着色是"直接估计面积"。

底层依赖 **alpha 混合**：`Color` 加了 `rgba()`，`Canvas` 加了 [`blendPixel`](../../src/include/gfx/core/canvas.hpp)（覆盖式 `putPixel` 的叠色版）。

## 6. 深度缓冲：遮挡不该靠排序

画家算法（按远近排序整个物体再依次绘制）处理不了**相互穿插**的三角形。深度缓冲是**逐像素**的：

```cpp
DepthBuffer depth(canvas.width(), canvas.height());   // 初始为远平面
// 三个顶点各自的深度（NDC z，越小越近）
fillTriangleDepth(canvas, depth, a, b, c, math::Vec3(za, zb, zc), color);
```

`testAndSet(x, y, z)` 只在**更近**时写入（等价 `GL_LESS`）。于是：

- 遮挡正确：近的盖住远的；
- **与绘制顺序无关**：先画近的还是先画远的，结果逐像素相同（测试锁定，示例程序里也是**故意乱序绘制**来演示这一点）。

深度值可以由 [View::projectToNdc](../../src/include/gfx/render/view.hpp) 产出（NDC 深度 ∈ [−1, 1]）。

## 7. 运行

```powershell
cmake --build build --target tutorial_triangle
build\tutorials\TriangleRaster.exe --output tutorials\triangle-raster\output\triangle-raster.png
build\tutorials\TriangleRaster.exe --window
```

结果图：[output/triangle-raster.png](output/triangle-raster.png)（上半三块对比；下半是三个重叠三角形**乱序绘制**的结果）。

| 参数 | 说明 |
|---|---|
| `--output <file>` | 结果 PNG 路径（默认 `output/triangle-raster.png`；目录需已存在） |
| `--window` | 打开窗口展示 |
| `--size WxH` | 画布尺寸（默认 900x600） |

程序会把三个 panel 的"中间色像素数"打到控制台，本身就是一次自检：点采样应为 **0**，两种抗锯齿应 **> 0**。

## 8. 权威实现与自测

- 权威实现：[triangle.hpp](../../src/include/gfx/render/triangle.hpp)（`edgeFunction` / `insideTriangle` / `barycentric` / `fillTriangle`（**唯一填充入口**，模式枚举 `TriangleFillMode`，可选 `samplesPerAxis` / `background`）/ `TriangleFiller` / `coverage` / `triangleFillWarningsEnabled`）、[depth.hpp](../../src/include/gfx/render/depth.hpp)（`DepthBuffer` / `fillTriangleDepth`）。抗锯齿实现位于 `detail`（`detail::fillTriangleCoverageAA` / `detail::fillTriangleSSAA`），不对外。
- 单元测试：`ctest --test-dir build -C Release -R test_triangle`（68 checks）：
  - **边界规则**：共享边的两个三角形恰好覆盖每个采样点一次（不重不漏）；
  - **三条路线互校**：`fillTriangle(SolidBBox)`、`fillTriangle(SolidScanline)`、`fillPolygon` 对同一三角形逐像素一致（顶点取非整数坐标，避开"采样点恰在边上"的规则歧义），并锁定"默认走增量式"；
  - **无关参数告警**：5 参调用无告警；`SolidScanline + samplesPerAxis`、`CoverageAA + background` 各打印一行对应告警；关闭 `triangleFillWarningsEnabled()` 后无告警；
  - 重心坐标：顶点权重、重心权重、权重和为 1、退化失败；
  - 覆盖率：内部 1、外部 0、边界 ∈ (0,1)（对 45° 边恰为 0.5）、共享边上两三角形之和为 1；
  - 抗锯齿：点采样无中间色，覆盖率着色有；SSAA 更接近覆盖率着色而非点采样；SSAA **不越界影响**包围盒之外的像素；
  - alpha 混合：不透明/全透明两端精确、`blendPixel` 叠色 vs `putPixel` 覆盖；
  - 深度缓冲：`testAndSet` 语义（更近通过、更远与同深度拒绝）、遮挡正确、**绘制顺序无关**。

## 9. 已知限制

- 只做**三角形**（凸）：凹多边形请用 [polygon-scan](../polygon-scan/README.md) 的扫描线填充，或自行三角化。
- 抗锯齿只有**覆盖率 / SSAA** 两种基础做法：没有 MSAA（子像素采样模板 + 解析覆盖率）、没有 FXAA/TAA/DLSS（讲义 p120 提到的当代方法，超出本库范围）。
- 深度缓冲是**单缓冲**：没有早期深度测试优化、没有透视校正插值（当前插值深度用的是屏幕空间的线性插值；3D 面片大面积渲染时会有偏差，属后续 3D 管线议题）。
- 无光照、无纹理、无背面剔除。
