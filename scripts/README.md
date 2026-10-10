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
| [package-homework2.cmd](package-homework2.cmd) | 一键组装"作业 2"提交包：组装 → 干净目录构建验证 → 跑测试 → 跑拓扑演示 → 收集 exe → 打 zip | 双击运行生成 `artifacts/homework2/学生_HW2.zip`；`scripts\package-homework2.cmd 张三` 生成对应命名 zip |
| [package-homework3.cmd](package-homework3.cmd) | 组装"作业 3"提交包：框架源码（main.cpp/RayTracer.cpp/RayTracer.h）+ 预编译 HW3.exe + OpenCV/VC 运行库（分发后免装环境即可运行）+ `gfx-results/`（自研 gfx 引擎头文件 + 演示源码 + CMakeLists + **静态链接可直接运行的 3 个 exe**（含窗口交互版）+ 结果图，附加），框架部分不重复触发 MSVC 构建 | 双击运行生成 `artifacts/homework3/学生_HW3.zip`；`scripts\package-homework3.cmd 202435450066林天佑` 生成对应命名 zip |

> 组装源（要改提交包内容请改这些，不要改 `artifacts/` 下的产物）：`tutorials/polygon-scan/{code,data,submit}` + `src/` + `tests/`。
> 作业 3 组装源：`artifacts/homework3/HW3/{main.cpp,RayTracer.cpp,RayTracer.h}`（课程框架副本，gitignore）+ `artifacts/homework3/bin/{HW4.exe,opencv_world4120.dll}`（预编译）+ `tutorials/ray-tracing-framework-scene/{submit,code,output}`（**`submit/使用说明.md` 就是进包的报告，改报告改这里**）+ `tutorials/ray-tracing/{code,output}` + `src/include/gfx` + `src/window_win32.cpp`（供窗口交互演示）。
