# design/ — 设计文档与决策记录（ADR）

> **用途**：存放设计文档与架构决策记录（ADR）：系统/模块设计、重构方案、决策归档。
> **目标读者**：进行设计讨论与决策追溯的开发者 / AI Agent。
> **关联文档**：[docs/README.md](../README.md)；[meta-rules.md](../conventions/meta-rules.md)（结论落盘纪律）。

> 状态：生效中（核验于 2026-09-09）。初始为空，随首个模块设计讨论生长。

## 导航（索引自维护）

| 文档 | 内容 |
|------|------|
| [adr-0001-gfx-math-canvas.md](adr-0001-gfx-math-canvas.md) | 首个模块决策：gfx_math 头文件子库 + Canvas 帧缓冲（✅ 采纳 2026-09-09） |
| [adr-0002-template-math.md](adr-0002-template-math.md) | gfx_math 全模板化：Mat&lt;N&gt;/Vec&lt;N&gt; + 别名，算法收敛 math::detail（✅ 采纳 2026-09-09） |
| [adr-0003-batch-backend.md](adr-0003-batch-backend.md) | GPU 切换预留分派点：batch 接口 + 运行时后端钩子，零依赖（✅ 采纳 2026-09-09） |
| [adr-0004-transform-chain.md](adr-0004-transform-chain.md) | 变换链机制 + "点 apply 变换" API：math 机制 / gfx 几何层，视图零拷贝（✅ 采纳 2026-09-09） |
| [adr-0005-raster-polygon-fill.md](adr-0005-raster-polygon-fill.md) | 光栅化模块：Bresenham 画线 + 扫描线填充（ET/AET + 半开区间规则）+ 自写 PNG 输出（✅ 采纳 2026-09-14） |
