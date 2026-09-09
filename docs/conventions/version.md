# version.md — 版本唯一事实源

> **用途**：规定版本号只出自一个权威文件，禁止在构建清单/打包脚本等处硬编码，防止多源漂移。
> **目标读者**：涉及版本号、构建目标版本、发布打包的开发者与 AI Agent。
> **关联文档**：[src/](../../src/README.md)；[CMakeLists.txt](../../CMakeLists.txt)；[packaging/](../../packaging/README.md)。

> 状态：生效中（核验于 2026-09-09）。随首个 gfx 模块落地创建 version.hpp。

## 规则

- 版本唯一事实源：`src/include/gfx/version.hpp`（随首个模块创建；含 `GFX_VERSION_MAJOR/MINOR/PATCH` 宏）。
- `src/CMakeLists.txt` 通过读取该文件（或 configure_file 引用）取版本，**禁止**在 CMakeLists / packaging 脚本中硬编码版本号。
- 发布时只改事实源文件；CMake 重新读取后构建产物随之更新。
- 语义化版本（major.minor.patch）；教学树内容更新不改 API 版本，API 兼容性变化才升位。

## 操作

1. 版本事实源更新（唯一手动步骤）。
2. 触发 CMake 重新配置/构建（自动拾取）。
3. 走 packaging/build_release.cmd 发布。
