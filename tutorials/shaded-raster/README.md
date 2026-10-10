# 光栅化着色管线（顶点属性 → 透视校正插值 → 逐像素着色）

> 主题：把"顶点带 uv/法向/切线 → 三角形光栅化 → 逐像素采样贴图 + 光照"这条 **GPU 里真正跑的路**做进 `gfx`，
> 并让 [normal-mapping](../normal-mapping/README.md) 那份纹理/切空间代码在**光栅化**路径上原样复用。
> 实现：[shading.hpp](../../../src/include/gfx/render/shading.hpp)；设计决策：[adr-0014](../../../docs/design/adr-0014-shaded-raster-pipeline.md)；测试：[test_shading.cpp](../../../tests/test_shading.cpp)。

## 学到的概念

1. **顶点属性与片元**：光栅化的本质是"把三角形内部的每个像素的属性**插值**出来"，再用一个**片元着色器**逐像素算颜色。这与光线跟踪（每像素一条光线求交）是两条完全不同的路。
2. **透视校正插值**：屏幕空间里 uv 不能直接线性插值 —— 远处那段会被"压缩"。正确做法是插值 `attr/w` 与 `1/w` 再相除。本演示里立方体侧面的砖缝走向就是靠它才不歪。
3. **深度（NDC z）不需要校正**：z_ndc 是 `1/w` 的仿射函数，在屏幕空间本就线性 —— 所以深度缓冲可以直接用未校正的重心坐标插值。
4. **近平面裁剪**：透视下相机后方的点 `w < 0`，直接除会把它**镜像翻进画面**；必须先按 `w ≥ 近平面` 把三角形裁掉。
5. **切空间要在片元里重建**：插值后的法向与切线不再严格正交，得重新正交化再组 TBN，否则凹凸会歪。
6. **分层**：这个管线不认识"光"和"材质" —— 每像素算什么颜色是**回调**注入的。所以贴图、法线贴图、纯光照都能配同一套管线。

## 怎么运行

```bat
cmake --build build --target tutorial_shaded_raster
build\tutorials\ShadedRaster                    :: 默认输出 output/shaded-raster.png
build\tutorials\ShadedRaster --help
```

| 参数 | 作用 |
|------|------|
| `--output <png>` | 结果路径（默认 `output/shaded-raster.png`，父目录自动创建）|
| `--size WxH` | 画布尺寸（默认 `800x600`）|
| `--strength s` | 凹凸强度（默认 1.0；**0 = 贴图退化成平整法向**，可用来对照）|

## 场景

三样东西**全部由三角形拼出来**（没有光线求交）：

| 物体 | 三角形数 | 法线贴图 |
|------|----------|----------|
| 地面（6×6 四边形） | 2 | 棋格（`checkerHeight`，按世界尺度平铺）|
| 立方体（6 个面） | 12 | 砖缝（`brickHeight`，每面自带法向与切线）|
| 球（28×44 经纬网格） | 2464 | 波纹（`waveHeight`，切空间与 `normalmap.hpp` 的球面口径一致）|

片元着色：`采样法线贴图 → decodeNormal → 重建 TBN → perturbNormal → shadeLocal(Blinn-Phong) → gamma(1/2.2)`。

## 结果（实测）

800×600，实跑：

```
  三角形数      : 2478
  着色片元数    : 436550
```

| 对比 | 结果 |
|------|------|
| `--strength 1` vs `--strength 0` | **259715 / 480000 像素不同（54.11%）**，最大通道差 **171** ⇒ 贴图经"采样 → TBN 扰动 → 逐像素光照"确实生效 |

产物：`output/shaded-raster.png`。

## 管线怎么用（核心 API）

```cpp
View view;                                   // 世界 → 像素（含投影）
view.setViewport(800, 600);
view.lookAt(eye, target, up);
view.setPerspective(fov, aspect, 0.1f, 100.0f);

DepthBuffer depth(800, 600);
ShadingTriangle tri{ {p0, n0, t0, uv0}, {p1, n1, t1, uv1}, {p2, n2, t2, uv2} };

fillTriangleShaded(canvas, view, tri, [&](const Fragment& f) -> math::Vec3 {
    // f.position / f.normal / f.tangent / f.uv 都已完成**透视校正**插值
    return shadeLocal(f.position, f.normal, viewDir(f), material, lights, noShadow);
}, &depth);
```

## 与光线跟踪那条路的关系

| | 光线跟踪（[ray-tracing](../ray-tracing/README.md)） | 光栅化着色管线（本主题） |
|---|---|---|
| 怎么定颜色 | 每像素发一条光线 **求交**，命中后递归 | 三角形**投影 + 插值**，逐像素跑片元着色器 |
| 法线贴图接入点 | `SurfaceHit::shadingNormal`（只影响光照） | `Fragment` 里的 normal/tangent/uv → 回调里扰动 |
| 阴影 | 阴影光线（真实遮挡） | 本主题未做（需要 shadow map 或额外的可见性 pass）|
| 共同件 | `Texture`（采样）、`TangentBasis`/`perturbNormal`/`makeNormalMap`、`shadeLocal` —— **两边通用** ||

## 关联文档

- 设计决策：[adr-0014](../../../docs/design/adr-0014-shaded-raster-pipeline.md)
- 上游：[adr-0013](../../../docs/design/adr-0013-normal-mapping.md)（纹理与法线贴图）、[adr-0008](../../../docs/design/adr-0008-triangle-raster.md)（三角形光栅化基线）
- 测试：[test_shading.cpp](../../../tests/test_shading.cpp)
