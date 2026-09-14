// demo_window.cpp —— 教学示例：第一个像素画到屏幕
//
// 编译（项目内）：cmake --build build --target tutorial_pixels 后运行
// 产物：build/tutorials/tutorial_pixels(.exe)
// 行为：320x240 窗口内画一条对角线（红色），并持续保持直到关闭窗口。

#include <gfx/canvas.hpp>
#include <gfx/color.hpp>
#include <gfx/window.hpp>

using gfx::Canvas;
using gfx::Color;
using gfx::rgb;

int main() {
    const int width = 320;
    const int height = 240;

    gfx::Window window("第一个像素", width, height);
    if (!window.isOpen()) return 1;

    Canvas& canvas = window.canvas();

    // 背景：深蓝
    canvas.clear(rgb(16, 24, 48));

    // 画红色对角线（逐个像素 putPixel）
    const int steps = width < height ? width : height;
    for (int i = 0; i < steps; ++i) {
        canvas.putPixel(i, i, rgb(220, 40, 40));
    }
    // 画中间一个点（示意“指定坐标放置像素”）
    canvas.putPixel(width / 2, height / 2, rgb(255, 255, 255));

    // 主循环：显示当前帧 + 处理关闭
    while (window.pollEvents()) {
        window.present();
    }
    return 0;
}
