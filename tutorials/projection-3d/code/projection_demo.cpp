// projection_demo.cpp —— 教学示例：正交投影 vs 透视投影（立方体线框，同框对比）
//
// 用途：用 gfx::View 的 3D 链路（lookAt + setOrthographic/setPerspective + drawLine3D）把**同一个**
//       立方体线框画两遍：左半屏正交投影、右半屏透视投影，直观对比两者差别（近大远小 / 平行性）。
// 权威实现：gfx::View（src/include/gfx/render/view.hpp，含近平面裁剪）；线段落笔用 gfx::drawLine（raster.hpp）。
// 构建：cmake --build build --target tutorial_projection_3d
// 运行：build/tutorials/Projection3D --output tutorials/projection-3d/output/projection-3d.png
//       build/tutorials/Projection3D --window --animate
//       build/tutorials/Projection3D --eye-dist 1.2 --output clip.png   （相机贴近：观察近平面裁剪）
//
// 参数：--output <png> / --window / --animate / --size WxH / --angle <度> / --eye-dist <距离> / --help

#include <gfx/core/canvas.hpp>
#include <gfx/core/color.hpp>
#include <gfx/render/png.hpp>
#include <gfx/render/view.hpp>
#include <gfx/render/window.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

using gfx::Canvas;
using gfx::Color;
using gfx::View;
using gfx::rgb;
using gfx::math::Vec3;

// ---- 配色 ----
const Color kBackground = rgb(14, 16, 24);
const Color kOrthoColor = rgb(90, 200, 160);   // 左：正交（青绿）
const Color kPerspColor = rgb(250, 190, 90);   // 右：透视（琥珀）
const Color kDivider = rgb(48, 52, 64);        // 中缝

constexpr int kDefaultWidth = 1000;
constexpr int kDefaultHeight = 600;
constexpr float kDefaultAngleDeg = 30.0f;
constexpr float kDefaultEyeDist = 6.0f;
constexpr float kOrthoHalfHeight = 2.2f;       // 正交视野半高（世界单位，足够装下旋转后的立方体）
constexpr float kFovYRadians = 0.7853982f;     // 45°

// ---- 立方体：边长 2、中心在原点 ----
const Vec3 kCubeVerts[8] = {
    Vec3(-1.0f, -1.0f, -1.0f), Vec3(1.0f, -1.0f, -1.0f), Vec3(1.0f, 1.0f, -1.0f), Vec3(-1.0f, 1.0f, -1.0f),
    Vec3(-1.0f, -1.0f,  1.0f), Vec3(1.0f, -1.0f,  1.0f), Vec3(1.0f, 1.0f,  1.0f), Vec3(-1.0f, 1.0f,  1.0f)};

const int kCubeEdges[12][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},     // 后面
    {4, 5}, {5, 6}, {6, 7}, {7, 4},     // 前面
    {0, 4}, {1, 5}, {2, 6}, {3, 7}};    // 竖棱

struct Options {
    bool useWindow = false;
    bool animate = false;
    std::string outputPath = "output/projection-3d.png";
    int width = kDefaultWidth;
    int height = kDefaultHeight;
    float angleDeg = kDefaultAngleDeg;
    float eyeDist = kDefaultEyeDist;
};

void printUsage() {
    std::cout <<
        "用法：\n"
        "  Projection3D [--output <out.png>] [--window [--animate]] [--size WxH]\n"
        "               [--angle <度>] [--eye-dist <距离>]\n"
        "  Projection3D --help\n"
        "\n"
        "参数：\n"
        "  --output <file>   结果 PNG 路径（默认 output/projection-3d.png；目录需已存在）\n"
        "  --window          打开窗口展示（左：正交；右：透视）\n"
        "  --animate         配合 --window：绕 Y 轴持续旋转\n"
        "  --size WxH        画布尺寸（默认 1000x600，左右各半）\n"
        "  --angle <度>      立方体绕 Y 轴的初始旋转角（默认 30）\n"
        "  --eye-dist <d>    相机到原点距离（默认 6；调到 ~1.2 以内可看到近平面裁剪）\n"
        "  --help            显示本帮助\n";
}

bool parseArgs(int argc, char** argv, Options& opt, int& exitCode) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            exitCode = 0;
            return false;
        }
        if (arg == "--window") {
            opt.useWindow = true;
        } else if (arg == "--animate") {
            opt.animate = true;
        } else if (arg == "--output" && i + 1 < argc) {
            opt.outputPath = argv[++i];
        } else if (arg == "--angle" && i + 1 < argc) {
            opt.angleDeg = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--eye-dist" && i + 1 < argc) {
            opt.eyeDist = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--size" && i + 1 < argc) {
            const std::string size = argv[++i];
            const std::size_t sep = size.find('x');
            if (sep == std::string::npos) {
                std::cerr << "错误：--size 需要 WxH 形式，例如 1000x600\n";
                exitCode = 2;
                return false;
            }
            opt.width = std::atoi(size.substr(0, sep).c_str());
            opt.height = std::atoi(size.substr(sep + 1).c_str());
            if (opt.width < 40 || opt.height < 40) {
                std::cerr << "错误：画布尺寸过小（至少 40x40）\n";
                exitCode = 2;
                return false;
            }
        } else {
            std::cerr << "错误：无法识别的参数 " << arg << "（用 --help 查看用法）\n";
            exitCode = 2;
            return false;
        }
    }
    if (opt.animate && !opt.useWindow) {
        std::cerr << "错误：--animate 需要与 --window 一起使用。\n";
        exitCode = 2;
        return false;
    }
    return true;
}

// 绕 Y 轴旋转 + 固定的绕 X 轴俯角（让立方体有立体感）
Vec3 rotateVertex(const Vec3& p, float angleRad) {
    const float cy = std::cos(angleRad), sy = std::sin(angleRad);
    const float x1 = p.x * cy + p.z * sy;
    const float z1 = -p.x * sy + p.z * cy;

    const float cx = std::cos(-0.35f), sx = std::sin(-0.35f);
    const float y2 = p.y * cx - z1 * sx;
    const float z2 = p.y * sx + z1 * cx;
    return Vec3(x1, y2, z2);
}

// 相机位置：固定视线方向（略微偏斜，透视的"近大远小"更明显）
Vec3 eyePosition(float eyeDist) {
    Vec3 dir;
    gfx::math::normalize(Vec3(0.45f, 0.30f, 0.84f), dir);
    return Vec3(dir.x * eyeDist, dir.y * eyeDist, dir.z * eyeDist);
}

// 画一帧：左半屏正交投影 + 右半屏透视投影（同一相机方向、同一立方体，只换投影方式）
void renderScene(Canvas& canvas, const Options& opt, float angleDeg) {
    canvas.clear(kBackground);

    const int halfW = opt.width / 2;
    const int height = opt.height;
    const float aspect = static_cast<float>(halfW) / static_cast<float>(height);
    const float angleRad = angleDeg * 3.14159265f / 180.0f;

    Vec3 rotated[8];
    for (int i = 0; i < 8; ++i) rotated[i] = rotateVertex(kCubeVerts[i], angleRad);

    const Vec3 eye = eyePosition(opt.eyeDist);
    const Vec3 target(0.0f, 0.0f, 0.0f);
    const Vec3 up(0.0f, 1.0f, 0.0f);

    // 左：正交投影（平行线保持平行；物体大小与距离无关）
    View ortho;
    ortho.setViewport(halfW, height).setViewportOrigin(0, 0);
    ortho.lookAt(eye, target, up);
    {
        const float halfH = kOrthoHalfHeight;
        const float halfWidthWorld = halfH * aspect;   // 按视口纵横比取世界范围，避免拉伸
        ortho.setOrthographic(-halfWidthWorld, halfWidthWorld, -halfH, halfH, 0.1f, 200.0f);
    }

    // 右：透视投影（近大远小；平行线在远处汇聚）
    View persp;
    persp.setViewport(halfW, height).setViewportOrigin(halfW, 0);
    persp.lookAt(eye, target, up);
    persp.setPerspective(kFovYRadians, aspect, 0.1f, 200.0f);

    for (const int* edge : kCubeEdges) {
        ortho.drawLine3D(canvas, rotated[edge[0]], rotated[edge[1]], kOrthoColor);
        persp.drawLine3D(canvas, rotated[edge[0]], rotated[edge[1]], kPerspColor);
    }

    // 中缝
    for (int y = 0; y < height; ++y) canvas.putPixel(halfW, y, kDivider);
}

int runWindow(const Options& opt) {
    gfx::Window window("正交投影 vs 透视投影（左：正交 / 右：透视）", opt.width, opt.height);
    if (!window.isOpen()) {
        std::cerr << "错误：窗口创建失败。\n";
        return 5;
    }
    float angle = opt.angleDeg;
    while (window.pollEvents()) {
        renderScene(window.canvas(), opt, angle);
        window.present();
        if (opt.animate) angle += 0.6f;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    int exitCode = 0;
    if (!parseArgs(argc, argv, opt, exitCode)) return exitCode;

    if (opt.useWindow) {
        const int code = runWindow(opt);
        if (code != 0) return code;
    }

    // 输出 PNG（离屏渲染同一帧，便于提交/文档引用）
    Canvas canvas(opt.width, opt.height);
    renderScene(canvas, opt, opt.angleDeg);

    std::string error;
    if (!gfx::savePng(canvas, opt.outputPath.c_str(), &error)) {
        std::cerr << "错误：保存 PNG 失败 —— " << error << "\n";
        return 5;
    }
    std::cout << "顶点/棱：8 顶点、12 棱；相机距离 " << opt.eyeDist
              << "，视场角 45°，旋转角 " << opt.angleDeg << "°\n"
              << "左半屏：正交投影（setOrthographic）；右半屏：透视投影（setPerspective）\n"
              << "已保存结果图：" << opt.outputPath << "\n";
    return 0;
}
