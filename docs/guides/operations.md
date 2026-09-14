# operations.md — 开发与发布操作手册

> **用途**：集中记录常用操作命令：配置、构建、测试、运行示例、打包发布。AGENT.md Commands 以此为详源。
> **目标读者**：执行开发/测试/发布的开发者与 AI Agent。
> **关联文档**：[AGENT.md](../../AGENT.md)（Commands 摘要）；[CMakeLists.txt](../../CMakeLists.txt)；[packaging/](../../packaging/README.md)。

> 状态：生效中（核验于 2026-09-14）。首个模块 0.1.0 已可配置/构建/测试/打包；作业 1（多边形填充）已提供一键脚本。

## 一键脚本（不想手敲命令时用这个）

| 脚本 | 作用 |
|------|------|
| `scripts\build-and-test.cmd` | 配置 + 构建 + 跑全部测试（`--clean` 先清理） |
| `scripts\run-polygon-fill.cmd` | 构建并运行多边形填充演示（双击出菜单：凸/凹/窗口动画/交互输入/非法输入） |
| `scripts\package-homework1.cmd [姓名]` | 组装作业 1 提交包（干净目录构建验证 + 跑测试 + 出图 + zip） |

> 脚本均为双击可用，并已固定 CRLF 行尾与 `chcp 65001`（中文显示）；清单见 [scripts/README.md](../../scripts/README.md)。

## 配置与构建

```bat
rem 配置（生成构建文件到 build/）
cmake -S . -B build

rem 全量构建（gfx_math 头文件子库 + gfx.dll + 测试 + 教学演示）
cmake --build build
```

> 按需构建：`cmake --build build --target gfx`（仅库）/ `--target tutorial_pixels` 等。

## 测试（自写轻量断言，零第三方依赖）

```bat
ctest --test-dir build -C Release --output-on-failure
rem 单测一个：ctest --test-dir build -R test_math
```

## 运行教学演示

```bat
cmake --build build --target tutorial_pixels
build\tutorials\tutorial_pixels.exe          rem 像素上屏演示（320x240 窗口，可关闭）
cmake --build build --target tutorial_math
build\tutorials\tutorial_math.exe            rem 矩阵变换数学演示
```

## 打包发布 API

```bat
rem 唯一发布入口：构建 Release 并产出 packaging/release/（include + lib + bin）
packaging\build_release.cmd
```

> 调用方接入：include → `packaging/release/include`；链接 → `release/lib` 导入库；运行时 → `release/bin` 的 dll 放 exe 旁或加入 PATH。

## 注意事项

- 版本号出自 src/include/gfx/version.hpp（见 [version.md](../conventions/version.md)），勿手改 CMake/打包脚本。
- 新增/改动公共头后重建 gfx.dll 并重跑测试；发布属 AGENT.md 的 Ask First 边界。
