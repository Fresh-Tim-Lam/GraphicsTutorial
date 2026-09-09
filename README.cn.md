# GraphicsTutorial（图形学教程）

> **用途**：GraphicsTutorial 是一个 Windows 端「图形渲染 API 从 0 造轮子」的教学向项目。从最基础的屏幕像素着色开始，逐模块演进，最终产出一个可被外部直接调用的 C++ 图形渲染 API（命名空间 `gfx`）。
> **目标读者**：想理解渲染器从零如何构建的开发者；仓库维护者与 AI Agent（见 [AGENT.md](AGENT.md)）。
> **关联文档**：[English mirror README.md](README.md) ｜ [docs 总索引](docs/README.md) ｜ [目录组织规范](docs/conventions/directory-organization.md)

> 状态：生效中（核验于 2026-09-09）。本文件为中文权威版；英文镜像见 README.md，冲突以本版为准。

## 项目目标

- 教学主线：**图形 API 从 0 造轮子**——不依赖现成图形库，仅用最基础的 Windows 屏幕像素着色能力起步。
- 最终产出：可调用的 `gfx` 渲染 API（共享库 + 头文件），配套**树状教学文档**与每主题可运行示例。
- 约束：Windows（当前阶段）；CMake 构建；语言 C/C++；不引入第三方图形依赖。

## 目录速览

| 目录 | 职责 |
|------|------|
| [src/](src/README.md) | `gfx` 渲染 API 源码（**权威实现，随模块演进**） |
| [tutorials/](tutorials/README.md) | **树状教学**：主题目录含 `.md` 讲解 + `code/` 可运行示例 |
| [docs/](docs/README.md) | 规范文档中心（conventions / guides / reference / design …） |
| [tests/](tests/README.md) | 测试 |
| [scripts/](scripts/README.md) | 可复用开发工具 |
| [resources/](resources/README.md) | 随项目分发的静态资源 |
| [packaging/](packaging/README.md) | 打包发布入口（唯一入口 `build_release.cmd`） |

## 快速开始

```bat
cmake -S . -B build
cmake --build build --config Release
```

发布 API 包：见 [packaging/build_release.cmd](packaging/build_release.cmd)。

## 文档入口

- [AGENT.md](AGENT.md) — Agent 与维护者第一站
- [docs/README.md](docs/README.md) — 文档总索引
