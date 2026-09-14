# scripts/ — 可复用开发者工具

> **用途**：存放正式、可复用的开发者脚本（构建辅助/生成/诊断等）。
> **目标读者**：运行与维护脚本的开发者及 AI Agent。
> **关联文档**：[docs/guides/operations.md](../docs/guides/operations.md)。

> 状态：生效中（核验于 2026-09-09）。首个脚本随实际需要添加。

## 职责一句话

放正式可复用的工具脚本；不放一次性/临时脚本（那类进 artifacts/）。

## 约定

- 脚本命名 kebab-case（见 [naming.md](../docs/conventions/naming.md)）。
- 脚本职责与用法登记到本 README 或 docs/guides/operations.md。
- **批处理脚本必须 CRLF 行尾**（`.gitattributes` 已固定 `*.cmd text eol=crlf`）：LF 会让 cmd.exe 解析错乱；脚本首行用 `chcp 65001` 以正确显示中文。

## 脚本清单（索引自维护）

| 脚本 | 用途 | 用法 |
|------|------|------|
| [build-and-test.cmd](build-and-test.cmd) | 一键：配置 + 构建 + 跑全部测试 | 双击运行；`scripts\build-and-test.cmd --clean` 先清理再全量重建 |
| [run-polygon-fill.cmd](run-polygon-fill.cmd) | 一键构建并运行"多边形扫描转换"演示（作业 1）：凸/凹样例、窗口扫描线动画、交互输入、非法输入演示 | 双击运行进入菜单；或 `scripts\run-polygon-fill.cmd convex\|concave\|animate\|window\|interactive\|invalid`；其余参数原样透传给 `PolygonFill` |
| [package-homework1.cmd](package-homework1.cmd) | 一键组装"作业 1"提交包：组装 → 干净目录构建验证 → 跑测试 → 生成结果图 → 打 zip | 双击运行生成 `artifacts/homework1/Homework1.zip`；`scripts\package-homework1.cmd 张三` 生成 `张三_Homework1.zip` |

> 组装源（要改提交包内容请改这些，不要改 `artifacts/` 下的产物）：`tutorials/polygon-scan/{code,data,submit}` + `src/` + `tests/`。
