// gfx_export.hpp —— gfx 库的 DLL 导出/导入宏（内部维护）
//
// 用途：统一 Windows 下 gfx 动态库的 __declspec(dllexport/dllimport) 切换。
// 目标读者：gfx 库实现者（公共 API 声明处需加 GFX_API）。
// 关联文档：docs/conventions/naming.md；docs/design/adr-0001-gfx-math-canvas.md
//
// 状态：生效中（2026-09-09）

#ifndef GFX_EXPORT_HPP
#define GFX_EXPORT_HPP

#if defined(_WIN32) || defined(_WIN64)
#  if defined(GFX_BUILDING_DLL)      // 构建 gfx.dll 时：导出
#    define GFX_API __declspec(dllexport)
#  else                              // 使用方：导入
#    define GFX_API __declspec(dllimport)
#  endif
#else
#  define GFX_API
#endif

#endif // GFX_EXPORT_HPP
