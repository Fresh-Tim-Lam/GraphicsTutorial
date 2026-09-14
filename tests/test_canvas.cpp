// test_canvas.cpp —— gfx::Canvas 帧缓冲测试（离屏、无需窗口）
//
// 运行：ctest --test-dir build -C Release -R test_canvas

#include "gfx_test.hpp"
#include <gfx/canvas.hpp>

using gfx::Canvas;
using gfx::Color;
using gfx::rgb;

int main() {
    // 基础尺寸
    Canvas c(8, 4);
    TEST_CHECK(c.width() == 8);
    TEST_CHECK(c.height() == 4);

    // 初始为黑色不透明
    TEST_CHECK(c.pixelAt(0, 0) == Color(0, 0, 0, 255));

    // putPixel 写入指定位置
    c.putPixel(3, 2, rgb(255, 0, 0));
    TEST_CHECK(c.pixelAt(3, 2) == rgb(255, 0, 0));
    // 邻居未被污染
    TEST_CHECK(c.pixelAt(2, 2) == Color(0, 0, 0, 255));
    TEST_CHECK(c.pixelAt(3, 1) == Color(0, 0, 0, 255));

    // 越界像素被忽略（不抛错、不改边界）
    c.putPixel(-1, 0, rgb(1, 2, 3));
    c.putPixel(8, 0, rgb(1, 2, 3));
    c.putPixel(0, -1, rgb(1, 2, 3));
    c.putPixel(0, 4, rgb(1, 2, 3));
    TEST_CHECK(c.pixelAt(0, 0) == Color(0, 0, 0, 255));
    TEST_CHECK(c.pixelAt(7, 3) == Color(0, 0, 0, 255));

    // 越界读取返回透明黑
    TEST_CHECK(c.pixelAt(100, 100) == Color(0, 0, 0, 0));

    // clear 全幅清色
    c.clear(rgb(10, 20, 30));
    TEST_CHECK(c.pixelAt(0, 0) == rgb(10, 20, 30));
    TEST_CHECK(c.pixelAt(7, 3) == rgb(10, 20, 30));

    // data() 行主序布局
    Canvas d(3, 2);
    d.putPixel(1, 1, rgb(9, 9, 9));
    TEST_CHECK(d.data()[1 * 3 + 1] == rgb(9, 9, 9));

    return gfx::test::summary();
}
