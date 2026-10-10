# src/ — 正式源码（gfx 渲染 API）

> **用途**：`gfx` 渲染 API 的权威实现所在，随教学模块演进。对外交付为动态库 + 头文件（含 gfx_math 头文件子库）。
> **目标读者**：实现 gfx 模块的开发者与 AI Agent。
> **关联文档**：[README.cn.md](../README.cn.md)；[architecture.md](../docs/refs/architecture.md)；[packaging/](../packaging/README.md)；[adr-0001](../docs/design/adr-0001-gfx-math-canvas.md)。

> 状态：生效中（核验于 2026-09-09）。首个模块（Canvas 帧缓冲 + Window 上屏 + gfx_math）0.1.0 已落地。

## 结构

公共头文件按**职责分层**（`include/gfx/` 下四个子层），每层有一个"层总入口"头（如 `core/core.hpp`），
可一次引入整层；层内具体头也可单独 include（如 `<gfx/render/raster.hpp>`）。

```
src/
├─ README.md
├─ CMakeLists.txt          # 解析 version.hpp；定义 gfx_math(INTERFACE) 与 gfx(SHARED)
├─ include/gfx/            # 公共头文件（对外分发），按职责分层
│   ├─ core/               # 基础设施层：画布 / 颜色 / 版本 / 导出宏（不依赖其它层）
│   │   ├─ core.hpp        # 层总入口（一次引入本层全部头）
│   │   ├─ color.hpp       # Color（RGBA8）+ rgb()/rgba() 构造 / blend() alpha 混合（ADR-0008）
│   │   ├─ canvas.hpp      # Canvas 帧缓冲（putPixel/clear/data…）+ blendPixel（alpha 叠色，ADR-0008）
│   │   ├─ version.hpp     # 版本唯一事实源（0.1.0）
│   │   └─ gfx_export.hpp  # DLL 导出宏（GFX_API）
│   ├─ geom/               # 几何与建模层：点 / 半边 B-rep 拓扑 / 光线求交与 BVH（**只依赖 math/，与 render/ 零耦合**）
│   │   ├─ geom.hpp        # 层总入口（一次引入本层全部头）
│   │   ├─ point.hpp       # Point2D/3D、PointBatch2D/3D（apply 变换，视图零拷贝）
│   │   ├─ halfedge.hpp    # 半边数据结构（B-rep）+ 五个欧拉操作（MVFS/MEV/MEF/KEMR/KFMRH）+ 拓扑一致性校验，ADR-0009
│   │   ├─ ray.hpp         # 光线求交（Ray/Aabb/HitRecord）：球/平面/三角形（Möller–Trumbore，双面）/任意轴圆柱（含端盖）/AABB（slab），ADR-0011
│   │   └─ bvh.hpp         # 层次包围盒 Bvh：中位数划分 + 最近命中剪枝（与图元类型解耦，回调按索引做精确求交），ADR-0011
│   ├─ render/             # 渲染层：光栅化 / 深度 / 相机 / 图像输出 / 窗口 / 光照 / 光线跟踪（依赖 core/ 与 math/，并**单向**依赖 geom/）
│   │   ├─ render.hpp      # 层总入口（一次引入本层全部头）
│   │   ├─ raster.hpp      # 光栅化：画线（**唯一签名** drawLine(float x0, float y0, float x1, float y1, color[, algo])——坐标平铺为 float，可同时接住 int/float 实参；Naive / DDA / Bresenham，**默认一律 Bresenham**；内部按四坐标是否为整数值**自动选择**纯整数 Bresenham / 浮点步进，DDA/Naive 各一版）+ 多边形轮廓 + 扫描线填充（ET/AET，半开区间规则，ADR-0005/0010）
│   │   ├─ triangle.hpp    # 三角形光栅化：inside test（top-left 边界规则）+ 重心坐标 + 填充（**唯一入口** fillTriangle(..., TriangleFillMode[, samplesPerAxis=0][, background=nullptr])，默认 SolidScanline，可选 SolidBBox；CoverageAA / SSAA 抗锯齿；对当前模式无意义的参数警告并忽略，见 triangleFillWarningsEnabled），ADR-0008
│   │   ├─ depth.hpp       # 深度缓冲（Z-Buffer）：逐像素深度测试 + 重心插值深度的三角形填充，ADR-0008
│   │   ├─ view.hpp        # 相机/视图：世界坐标 ↔ 像素坐标（2D 正交等比 + fit + 视口原点；3D lookAt/正交/透视投影 + 近平面裁剪 + drawLine3D + 视体参数与换算/NDC 深度/投影分解/相机基与反投影，ADR-0006）
│   │   ├─ png.hpp         # 图像输出：自写 PNG 编码（CRC32/Adler32 + zlib 存储块，零依赖，ADR-0005）
│   │   ├─ window.hpp      # Window（把 Canvas 整帧上屏）
│   │   ├─ lighting.hpp    # 光照模型：Material / Light + shadeLocal（环境光 + 自发光 + Lambert 漫反射 + Blinn-Phong 高光 + 阴影；阴影由**回调**注入解耦）+ offsetAlong/自交偏移常量，ADR-0012
│   │   ├─ raytracer.hpp   # 递归光线跟踪：SurfaceHit（几何 + 材质指针 + 可选着色法向）+ RayTracer（trace 递归：法向定向/偏移/阴影/镜面反射/规则透射/全反射/深度；render 出图：子像素中心超采样 + 可选伽马；求交由**回调**注入解耦），ADR-0012
│   │   ├─ texture.hpp     # 纹理：WrapMode（Repeat/Clamp/Mirror）+ TextureFilter（Nearest/Bilinear）+ Texture（内存 RGB 贴图 + 归一化 uv 采样），与渲染路径无关，ADR-0013
│   │   ├─ normalmap.hpp   # 法线贴图：TangentBasis + decodeNormal/perturbNormal + 球/平面解析切空间 + makeNormalMap（高度场→法线图，内置棋格/砖缝/波纹），ADR-0013
│   │   ├─ shading.hpp     # 光栅化着色管线：VertexAttributes/Fragment/FragmentShader + fillTriangleShaded（透视校正插值 + 近平面裁剪 + 逐像素着色回调；复用 triangle/depth/time 件），ADR-0014
│   │   └─ shadowmap.hpp   # 阴影贴图：ShadowMap（光源视角只写深度的 pass + 主 pass 深度比较；法向偏移 + 斜率缩放容差治自阴影，PCF 软化），ADR-0015
│   └─ math/               # 数学基础层（gfx_math 头文件子库，5 个头，零平台依赖）
│       ├─ vec.hpp                # Vec<N> 模板 + 别名 vec2/vec3/vec4；齐次归一化 fromHomogeneous；光线追踪向量运算 reflect/refract（ADR-0002/0007/0011）
│       ├─ mat.hpp                # Mat<N> 模板 + 别名 mat2/3/4；算法收敛 math::detail；向量变换 applyVector + 仿射拆解 translationOf/linearPartOf/isAffine（ADR-0007）
│       ├─ transform.hpp          # transform2d/3d 原语（含换基、绕轴/绕点的闭式+组合双实现、错切 shear、欧拉角 euler、带原点坐标基 frame、逆变换闭式）+ Transform<N> 链（惰性折叠缓存，见 ADR-0004/0007）
│       ├─ batch.hpp              # 批量变换 + 后端钩子（GPU 切换预留，见 ADR-0003）
│       └─ math.hpp               # 汇总 include（层总入口）
└─ window_win32.cpp        # Window 的 Win32 GDI 实现（内部实现，不对外）
```

## 分层语义

- **core** = 基础设施（画布 / 颜色 / 版本 / 导出宏）。不依赖其它层，最底层。
- **geom** = 几何与建模（点、半边 B-rep 拓扑、光线求交与 BVH）。**只依赖 `math/`，与 `render/` 零耦合** —— 这是刻意保留的界线，
  将来可把本层单独拆库（几何/建模库不需要渲染）。
- **render** = 渲染（光栅化、深度、相机、图像输出、窗口、光照、光线跟踪）。依赖 `core/`（画布/颜色）与 `math/`，并**单向**依赖 `geom/`（光线跟踪复用 `geom` 的命中记录 `HitRecord` 与求交函数；`geom` 仍不依赖 `render`，这一条界线不变）。
- **math** = 数学基础（`vec`/`mat`/`transform`/`batch`）。独立头文件子库，拥有独立目标 `gfx_math`。

## include 约定

- 唯一 include 根 = `src/include`（CMake 中 `target_include_directories` 已加入）。
- 库内部头文件之间**一律使用以 `gfx/` 开头的全路径**，如 `#include "gfx/core/color.hpp"`，避免相对路径随目录调整而失效。
- 使用方既可 `#include <gfx/render/raster.hpp>` 单独引入某头，也可 `#include <gfx/core/core.hpp>` / `<gfx/geom/geom.hpp>` / `<gfx/render/render.hpp>` 一次引入整层。

## 约束

- 版本唯一事实源：`include/gfx/core/version.hpp`；CMake 解析它，禁止硬编码（见 [version.md](../docs/conventions/version.md)）。
- 只用最基础的 Windows 屏幕像素着色能力（GDI DIB/BitBlt）；不引入第三方图形库（从 0 造轮子）。
- 平台相关实现集中在 .cpp 内部；公共头保持平台无关观感。
- 命名：C++ 标识符英文、注释中文（见 [naming.md](../docs/conventions/naming.md)）。
