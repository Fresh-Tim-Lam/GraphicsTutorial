// version.hpp —— 版本唯一事实源（内部维护，勿在 CMake/打包脚本硬编码版本号）
//
// 用途：全仓唯一权威版本号。升级请只改本文件（docs/conventions/version.md 规则）。
// 目标读者：维护者与 AI Agent（发布时）。
// 关联文档：docs/conventions/version.md；src/CMakeLists.txt（解析读取）。
//
// 状态：生效中（2026-09-09）

#ifndef GFX_VERSION_HPP
#define GFX_VERSION_HPP

#define GFX_VERSION_MAJOR 0
#define GFX_VERSION_MINOR 1
#define GFX_VERSION_PATCH 0

#define GFX_VERSION_STRING "0.1.0"

#endif // GFX_VERSION_HPP
