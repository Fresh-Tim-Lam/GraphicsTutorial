# GraphicsTutorial

**用途**：GraphicsTutorial 是一个 Windows 端「图形渲染 API 从 0 造轮子」的教学向项目。逐步从最基础的屏幕像素着色（Windows 底层）开始，构建出可被外部直接调用的 C++ 图形渲染 API（命名空间 `gfx`，动态库 + 头文件分发）。
**目标读者**：想要了解"渲染器从零如何长出来"的开发者；仓库维护者与 AI Agent（读 [AGENT.md](AGENT.md)）。
**关联文档**：[中文总览 README.cn.md](README.cn.md) ｜ [docs 总索引](docs/README.md) ｜ [目录组织规范](docs/conventions/directory-organization.md)

> 状态：生效中（核验于 2026-09-09）。本文件为面向国际读者的镜像版，权威版为 README.cn.md（冲突以中文版为准）。

## What / 这是什么

- 教学对象：**图形 API 从 0 造轮子**——不依赖现成图形库，仅用最基础的 Windows 屏幕像素着色能力。
- 最终产出：可调用的 `gfx` 渲染 API（共享库 + 头文件），配套树状教学文档与可运行示例。
- 约束：Windows（当前阶段）；构建使用 CMake；不引入第三方图形依赖。

## Repo Layout / 目录速览

| Path | Responsibility |
|------|----------------|
| [src/](src/README.md) | `gfx` 渲染 API 源码（权威实现，随模块演进） |
| [tutorials/](tutorials/README.md) | 树状教学文档 + 每主题可运行示例（md + code） |
| [docs/](docs/README.md) | 规范文档中心（conventions/guides/refs/design/...） |
| [tests/](tests/README.md) | 测试 |
| [benchmarks/](benchmarks/README.md) | 正式性能基准（可复现） |
| [scripts/](scripts/README.md) | 可复用开发工具 |
| [resources/](resources/README.md) | 随项目分发的静态资源 |
| [packaging/](packaging/README.md) | 打包发布入口（唯一入口 `build_release.cmd`） |

## Quick Start / 快速开始

```bat
cmake -S . -B build
cmake --build build --config Release
```

发布 API 包：见 [packaging/build_release.cmd](packaging/build_release.cmd)。

## Docs / 文档

- [AGENT.md](AGENT.md) — Agent 与维护者第一站
- [docs/README.md](docs/README.md) — 文档总索引
