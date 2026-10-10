# pixels-on-screen —— 把第一个像素画到屏幕

> **用途**：第一个可见成果——用 Canvas 在内存里逐像素画东西，再整帧显示到窗口。
> **目标读者**：刚接触"图形如何上屏"的读者。
> **关联文档**：权威实现 [canvas.hpp](../../src/include/gfx/core/canvas.hpp) 与 [window.hpp](../../src/include/gfx/render/window.hpp)；ADR [adr-0001](../../docs/design/adr-0001-gfx-math-canvas.md)。

> 状态：生效中（核验于 2026-09-09）。

## 一句话

图形上屏 = 先在内存画布上决定每个像素的颜色，再一次把整块画布拷到屏幕。

## 关键概念

- **Canvas（帧缓冲）**：一块内存，宽×高个像素。用 `putPixel(x, y, color)` 把颜色写到指定坐标。
- **越界策略**：坐标超出画布会被忽略（本阶段裁剪策略，见 canvas.hpp 注释）。
- **Window（上屏）**：把 Canvas 整帧交给窗口显示。教学实现走 Windows GDI：内存位图（DIB）+ `BitBlt` 一次拷贝，避免逐像素 `SetPixel` 的慢与闪烁。

## 像素绘制三步法

1. `gfx::Window win("标题", 宽, 高);` —— 得到窗口与其内部 Canvas。
2. 在 `win.canvas()` 上逐像素绘制（`putPixel` / 循环画线…）。
3. `win.present()` 显示这一帧；`win.pollEvents()` 处理关闭事件。

## 权威实现

`src/include/gfx/core/canvas.hpp`（纯内存帧缓冲，可离线测试）+ `src/window_win32.cpp`（Win32 GDI 上屏）。测试见 `ctest -R test_canvas`。

## code/（演示）

- [demo_window.cpp](code/demo_window.cpp)：打开 320×240 窗口，画一条"从左上到右下"的对角线并保持显示，按窗口 X 关闭。
