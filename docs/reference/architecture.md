# architecture.md — 架构总览与数据流

> **用途**：记录 GraphicsTutorial 的整体目录结构（摘要同步自 directory-organization.md，冲突以 directory-organization 为准）与核心数据流；随开发持续补充模块职责。
> **目标读者**：需要系统级理解的开发者 / AI Agent / 读者。
> **关联文档**：[directory-organization.md](../conventions/directory-organization.md)；[src/](../../src/README.md)。

> 状态：生效中（核验于 2026-09-09）。以下为初始骨架，随模块演进补充。

## 总览

- 从 0 造轮子：仅用 Windows 最基础屏幕像素着色能力，向上构建可调用的 `gfx` 渲染 API。
- 交付形态：动态库（DLL + 导入库）+ 头文件，命名空间 `gfx`；打包入口 packaging/build_release.cmd。

## 数据流（一句话，随开发补充）

Windows 屏幕像素缓冲（最基础输出） → `gfx` 窗口/画布封装 → 绘制原语与渲染对象（模块逐层扩展）→ 供调用方使用。

## 模块职责（待补充）

| 模块 | 职责 | 位置（规划） |
|------|------|------------|
| window（规划） | 创建窗口与像素画布 | src/ 首个模块候选 |
| … | … | … |
