# projection-3d —— 正交投影 vs 透视投影（立方体线框）

> **用途**：讲清 3D 的"观察 → 投影 → 像素"链路，以及**正交投影**与**透视投影**的本质差别；并说明为什么画 3D 线段必须先做**近平面裁剪**。
> **目标读者**：要从 3D 顶点画出图（线框/网格）的人；学习投影变换的读者。
> **关联文档**：权威实现 [src/include/gfx/render/view.hpp](../../src/include/gfx/render/view.hpp)；设计决策 [adr-0006](../../docs/design/adr-0006-camera-view.md)；2D 视图 [view-projection](../view-projection/README.md)；线段落笔 [polygon-scan](../polygon-scan/README.md)（[raster.hpp](../../src/include/gfx/render/raster.hpp)）。

> 状态：生效中（2026-09-16）。权威实现见 `src/include/gfx/render/view.hpp`。

## 1. 3D 顶点怎么变成像素

```
世界坐标 ──lookAt──> 相机空间 ──投影矩阵──> 裁剪空间 ──透视除法──> NDC ──视口变换──> 像素
 (x,y,z)              (相机为原点)         (齐次 x,y,z,w)        (各分量/w)   (y 翻转)
```

`View` 把这五步都封好了：

| 步骤 | 接口 |
|------|------|
| 相机（世界 → 相机） | `lookAt(eye, target, up)` |
| 投影（相机 → 裁剪） | `setOrthographic(...)` 或 `setPerspective(...)` |
| 两步合并 | `viewProjectionMatrix()` |
| 单点 → 像素 | `projectToPixel(Vec3)` |
| **线段** → 像素（含裁剪） | `projectSegment(a, b, outA, outB)` |
| 线段 → 像素并绘制 | `drawLine3D(canvas, a, b, color)` |

## 2. 两种投影的本质差别

| | 正交投影 | 透视投影 |
|---|---|---|
| 构造 | `setOrthographic(left, right, bottom, top, zNear, zFar)` | `setPerspective(fovY, aspect, zNear, zFar)` |
| 平行线 | **保持平行** | 在远处**汇聚**（消失点） |
| 物体大小 | 与距离**无关** | **近大远小** |
| 齐次 w | 恒为 1（无透视除法效果） | = 相机前方的深度 |
| 典型用途 | 工程制图、2.5D 游戏、UI | 拟真 3D |

本示例把**同一个立方体、同一个相机方向**画两遍，只换投影方式，于是上面的差别直接可见。用独立脚本量化（见第 6 节），立方体 4 条互相平行且等长的棱在屏幕上的长度：

- 正交：`15.5、15.5、15.5、15.5` px —— **严格等长**（最长/最短 = 1.0000）
- 透视：`49.4、67.0、68.6、51.7` px —— 不等（最长/最短 = 1.3888），正是"近大远小"

## 3. 运行

```powershell
# 默认：左半屏正交、右半屏透视，输出 PNG
build\tutorials\Projection3D.exe --output tutorials\projection-3d\output\projection-3d.png

# 开窗看，并绕 Y 轴持续旋转
build\tutorials\Projection3D.exe --window --animate

# 相机贴到立方体内部：观察近平面裁剪（见第 5 节）
build\tutorials\Projection3D.exe --eye-dist 1.2 --output tutorials\projection-3d\output\projection-3d-near.png
```

| 参数 | 说明 |
|---|---|
| `--output <file>` | 结果 PNG 路径（默认 `output/projection-3d.png`；目录需已存在） |
| `--window` | 打开窗口展示 |
| `--animate` | 配合 `--window`：绕 Y 轴持续旋转 |
| `--size WxH` | 画布尺寸（默认 1000x600，左右各半） |
| `--angle <度>` | 立方体绕 Y 轴的初始旋转角（默认 30） |
| `--eye-dist <d>` | 相机到原点距离（默认 6；调到 ~1.2 以内可看到近平面裁剪） |

结果图：[output/projection-3d.png](output/projection-3d.png)（默认）、[output/projection-3d-near.png](output/projection-3d-near.png)（近距裁剪）。

## 4. 代码要点

```cpp
// 左半屏：正交投影
View ortho;
ortho.setViewport(halfW, height).setViewportOrigin(0, 0);   // 视口原点 = 分屏的关键
ortho.lookAt(eye, target, up);
ortho.setOrthographic(-ew, ew, -eh, eh, 0.1f, 200.0f);      // 半宽按视口纵横比取，避免拉伸

// 右半屏：透视投影
View persp;
persp.setViewport(halfW, height).setViewportOrigin(halfW, 0);
persp.lookAt(eye, target, up);                              // 同一相机方向，便于公平对比
persp.setPerspective(0.7853982f, aspect, 0.1f, 200.0f);      // fovY 用**弧度**（45°）

for (const int* e : kCubeEdges) {
    ortho.drawLine3D(canvas, verts[e[0]], verts[e[1]], kOrthoColor);
    persp.drawLine3D(canvas, verts[e[0]], verts[e[1]], kPerspColor);
}
```

- **分屏靠 `setViewportOrigin`**：`View` 描述的是"一块视口矩形"，两块并排各持一个 View 即可。
- **`drawLine3D` 内部**：`projectSegment`（投影 + 裁剪）→ `gfx::drawLine` 的**浮点端点版**。用浮点版很关键——投影出的像素坐标几乎总带小数，若先取整会丢亚像素精度（见 [view-projection 第 6 节](../view-projection/README.md)）。
- **正交的半宽要按纵横比取**（`eh * aspect`），否则立方体会被拉扁。

## 5. 近平面裁剪：为什么必须有

透视投影下，相机**后方**的点齐次 `w < 0`。此时若直接做 `x/w`，点会被**镜像翻到画面里**——得到一条完全不存在的"鬼影线"。

`projectSegment` 的做法：先在裁剪空间把端点裁到 `w = zNear`（近平面，其 NDC 深度为 −1），再做除法。

用 `--eye-dist 1.2` 把相机放进立方体里，差异立刻可见（同一份代码，仅相机距离不同）：

| 指标（右半屏·透视） | eyeDist = 6（正常） | eyeDist = 1.2（相机在立方体内） |
|---|---|---|
| 输出像素数 | 2145 | 839 |
| "不做裁剪"会画出的画面内像素 | 2145（与裁剪结果相同） | 816，其中 6 条线段的端点被除到极远无法像素化 |
| 输出与"无裁剪结果"的差异 | 0 | 31 像素 ⇒ **裁剪确实改变了绘制结果** |

> 正交投影下 `w ≡ 1`，永远不会触发裁剪——这也是正交投影"不会出现鬼影"的原因。

## 6. 视体参数、深度与两阶段分解

投影不是"算完就丢"——`View` 把参数留下来了（见 [ADR-0006](../../docs/design/adr-0006-camera-view.md) D8）：

```cpp
view.setPerspective(0.7853982f, 800.0f / 600.0f, 0.1f, 100.0f);

view.frustum();        // ViewFrustum{left, right, bottom, top, nearZ, farZ}
                       //   透视下是**近平面矩形**（l/r/b/t）+ 深度范围；正交下就是那个长方体
view.fovYRadians();    // 0.7853982（弧度）
view.aspect();         // 1.3333

// fovY + aspect ↔ l/r/b/t：同一视体的两种参数化，可互算
const gfx::ViewFrustum f = gfx::View::frustumOfPerspective(0.7853982f, 1.3333f, 0.1f, 100.0f);
float fov = 0.0f, aspect = 0.0f;
gfx::View::perspectiveOfFrustum(f, fov, aspect);   // 反算回去

// NDC 深度（z_ndc ∈ [−1,1]）：深度排序 / 深度测试用
gfx::math::Vec3 ndc;
if (view.projectToNdc(gfx::math::Vec3(0.0f, 0.0f, 0.0f), ndc)) { /* ndc.z 即深度 */ }

// 两阶段分解：projectionMatrix() == ortho · perspToOrtho（本节第 5 个公式）
const gfx::View::ProjectionDecomposition d = view.projectionDecomposition();
```

3D 侧还有几个常用反查（见 [ADR-0006](../../docs/design/adr-0006-camera-view.md) D9）：

| 接口 | 用途 |
|------|------|
| `cameraRight()/cameraUp()/cameraBack()`、`eyePosition()` | 相机三根基向量与位置（此前只能拿到矩阵） |
| `cameraToWorldMatrix()` | `C = V⁻¹`：反投影与推导用，第 4 列就是相机位置 |
| `modelViewMatrix(M)` | `V·M`：把**模型空间**直接送到相机空间 |
| `pixelToRay(px, py, origin, dir)` | 屏幕像素 → **世界射线**（3D 拾取），与 `projectToPixel` 互为往返 |

## 7. 权威实现与自测

- 权威实现：[src/include/gfx/render/view.hpp](../../src/include/gfx/render/view.hpp)（`lookAt` / `setOrthographic` / `setPerspective` / `projectSegment` / `drawLine3D`）；线段落笔 [raster.hpp](../../src/include/gfx/render/raster.hpp)。
- 单元测试：`ctest --test-dir build -C Release -R test_view`（350 checks；含近平面裁剪、视口原点、`drawLine3D`、视体参数与换算、NDC 深度、投影分解、相机基/位置/反投影、相对运动不变性）。
- 端到端独立校验（本示例）：`python artifacts/projection-3d-check/verify_projection.py <png> [eyeDist]` —— 该脚本**自己实现**矩阵与光栅化，与本示例输出逐像素比对：
  - 正常场景：**0 像素差异**；正交棱长比 1.0000、透视 1.3888（投影特性正确）；
  - 极端裁剪场景：差异 4 像素（C++ 用 `float`、脚本用 `double`，端点被投到数千像素外时的精度差）。

## 8. 已知限制

- 只支持**线框**：没有 3D 三角形填充、没有深度缓冲，所以**不做隐藏线消隐**（线框全画出来，前后棱都可见）。
- 只有**近平面**裁剪：远平面/视锥侧面未裁剪（代价是远处的线可能被投影到画面内，但数值上仍有限）。
- 无光照、无纹理、无抗锯齿（硬边最近像素）。
