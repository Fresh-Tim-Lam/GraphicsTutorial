# src/ — 正式源码（gfx 渲染 API）

> **用途**：`gfx` 渲染 API 的权威实现所在，随教学模块演进。对外交付为动态库 + 头文件（含 gfx_math 头文件子库）。
> **目标读者**：实现 gfx 模块的开发者与 AI Agent。
> **关联文档**：[README.cn.md](../README.cn.md)；[architecture.md](../docs/refs/architecture.md)；[packaging/](../packaging/README.md)；[adr-0001](../docs/design/adr-0001-gfx-math-canvas.md)。

> 状态：生效中（核验于 2026-09-09）。首个模块（Canvas 帧缓冲 + Window 上屏 + gfx_math）0.1.0 已落地。

## 结构

```
src/
├─ README.md
├─ CMakeLists.txt          # 解析 version.hpp；定义 gfx_math(INTERFACE) 与 gfx(SHARED)
├─ include/gfx/            # 公共头文件（对外分发）
│   ├─ version.hpp         # 版本唯一事实源（0.1.0）
│   ├─ gfx_export.hpp      # DLL 导出宏
│   ├─ color.hpp           # Color（RGBA8）
│   ├─ canvas.hpp          # Canvas 帧缓冲（putPixel/clear/data…）
│   ├─ raster.hpp          # 光栅化：Bresenham 画线 + 多边形轮廓 + 扫描线填充（ET/AET，半开区间规则，ADR-0005）
│   ├─ png.hpp             # 图像输出：自写 PNG 编码（CRC32/Adler32 + zlib 存储块，零依赖，ADR-0005）
│   ├─ point.hpp           # 几何层：Point2D/3D、PointBatch2D/3D（apply 变换，视图零拷贝）
│   ├─ window.hpp          # Window（把 Canvas 整帧上屏）
│   └─ math/               # gfx_math 头文件子库（5 个头）
│       ├─ vec.hpp                # Vec<N> 模板 + 别名 vec2/vec3/vec4（ADR-0002）
│       ├─ mat.hpp                # Mat<N> 模板 + 别名 mat2/3/4；算法收敛 math::detail
│       ├─ transform.hpp          # transform2d/3d 原语（含换基、绕轴/绕点的闭式+组合双实现）+ Transform<N> 链（惰性折叠缓存，见 ADR-0004）
│       ├─ batch.hpp              # 批量变换 + 后端钩子（GPU 切换预留，见 ADR-0003）
│       └─ math.hpp        # 汇总 include
└─ window_win32.cpp        # Window 的 Win32 GDI 实现（内部实现，不对外）
```

## 约束

- 版本唯一事实源：`include/gfx/version.hpp`；CMake 解析它，禁止硬编码（见 [version.md](../docs/conventions/version.md)）。
- 只用最基础的 Windows 屏幕像素着色能力（GDI DIB/BitBlt）；不引入第三方图形库（从 0 造轮子）。
- 平台相关实现集中在 .cpp 内部；公共头保持平台无关观感。
- 命名：C++ 标识符英文、注释中文（见 [naming.md](../docs/conventions/naming.md)）。
