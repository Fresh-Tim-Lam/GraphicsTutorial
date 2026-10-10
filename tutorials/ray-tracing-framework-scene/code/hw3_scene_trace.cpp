// hw3_scene_trace.cpp —— 教学示例：用 gfx 复刻"课程框架（作业三）的原始场景"并与框架基准图做等价性比对
//
// 用途：把老师原始 main.cpp 里构造的**同一场景**（房间 6 面 + 玻璃球 + 绿球 + 蓝立方 + 3 点光源）
//       用 gfx 的类型与函数逐项复刻，用**与框架 render_scene 完全相同的相机与像素→射线公式**
//       生成主光线，开 BVH 加速，渲染出 hw3_gfx_result.png；本教程的**目的就是与课程框架的基准图
//       做逐像素等价性比对**（基准图由临时驱动 artifacts/homework3/_verify/baseline.cpp 生成）。
//
// 权威实现：gfx/render/lighting.hpp（shadeLocal）、gfx/render/raytracer.hpp（RayTracer）、
//           gfx/geom/ray.hpp（求交）、gfx/geom/bvh.hpp（Bvh）、gfx/render/png.hpp（savePng）、
//           gfx/render/view.hpp（View，仅用于"相机公式等价性自检"）。
// 构建：cmake --build build --target tutorial_hw3_scene
// 运行：build/tutorials/Hw3SceneTrace --output tutorials/ray-tracing-framework-scene/output/hw3_gfx_result.png
//
// 参数：--output <png>（默认 output/hw3_gfx_result.png，父目录自动创建）/ --help
// 说明：分辨率 800x600、maxDepth=5、每像素 1 采样、关闭伽马——全部固定为框架原始口径，
//       以便与基准图逐像素对齐（故不提供 --size / --samples / --depth 开关）。

#include <gfx/core/canvas.hpp>
#include <gfx/core/color.hpp>
#include <gfx/geom/geom.hpp>
#include <gfx/render/png.hpp>
#include <gfx/render/raytracer.hpp>
#include <gfx/render/view.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <io.h>   // _isatty / _fileno（双击运行时判断 stdin 是否为终端）
#ifndef NOMINMAX
#define NOMINMAX      // 避免 windows.h 的 min/max 宏破坏 std::min / std::max
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>   // SetConsoleOutputCP（控制台按 UTF-8 输出，避免中文乱码）
#endif

namespace {

using gfx::Aabb;
using gfx::Bvh;
using gfx::Canvas;
using gfx::HitRecord;
using gfx::Light;
using gfx::Material;
using gfx::Ray;
using gfx::RayTracer;
using gfx::SurfaceHit;
using gfx::View;
using gfx::math::Vec3;

// 双击运行（命令行无参数）时渲染完暂停一下，避免控制台一闪而过、看不到输出路径。
// 仅当 stdin 是终端时才暂停，脚本 / 管道调用不受影响。
void pauseIfInteractive() {
#if defined(_WIN32)
    if (_isatty(_fileno(stdin))) {
        std::cout << "\n按回车键退出 ..." << std::flush;
        std::cin.get();
    }
#endif
}

// 框架默认值：与框架 Material() 构造函数完全一致（注意框架的 shininess 默认是 0，
// 而 gfx::Material 默认是 32——本教程显式覆写为 0，确保数值口径一致）。
Material frameworkDefault() {
    Material m;
    m.diffuse = Vec3(0.0f, 0.0f, 0.0f);
    m.specular = Vec3(0.0f, 0.0f, 0.0f);
    m.emission = Vec3(0.0f, 0.0f, 0.0f);
    m.shininess = 0.0f;
    m.reflectivity = 0.0f;
    m.transparency = 0.0f;
    m.refractiveIndex = 1.0f;
    return m;
}

// 场景图元：类型标签 + 参数 + 材质 + 预计算 AABB（BVH 只吃 AABB，精确求交在回调里按索引分派）
struct Prim {
    enum Kind { Plane, Sphere, Box };
    Kind kind = Plane;
    Vec3 p0, p1;              // 平面：p0=面上一点、p1=法向；球：p0=球心；盒：p0=min、p1=max
    float radius = 0.0f;      // 球半径
    Material material;
    Aabb box;
};

Vec3 compMin(const Vec3& a, const Vec3& b) { return Vec3(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)); }
Vec3 compMax(const Vec3& a, const Vec3& b) { return Vec3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)); }

// 轴对齐盒求交：**逐行照抄框架 Cube::intersect**（含 0.001 距离阈值与按面判定的法向），
// 以保证与框架几何口径完全一致（gfx 目前没有盒图元，故此处内联一个等价的）。
bool intersectBox(const Ray& ray, const Vec3& mn, const Vec3& mx, HitRecord& out) {
    Vec3 t1, t2;
    for (int i = 0; i < 3; ++i) {
        const float d = ray.direction[i];
        t1[i] = (mn[i] - ray.origin[i]) / d;   // 与框架一致：逐分量相除（d≈0 时结果由 IEEE 规则决定）
        t2[i] = (mx[i] - ray.origin[i]) / d;
    }
    Vec3 tmin = compMin(t1, t2);
    Vec3 tmax = compMax(t1, t2);
    const float t_near = std::max(std::max(tmin.x, tmin.y), tmin.z);
    const float t_far = std::min(std::min(tmax.x, tmax.y), tmax.z);
    if (t_near > t_far || t_far < 0.001f) return false;

    const float t = (t_near > 0.001f) ? t_near : t_far;
    if (!(t > 0.001f)) return false;

    const Vec3 p = ray.origin + ray.direction * t;
    const float eps = 0.0001f;
    Vec3 n;
    if (std::fabs(p.x - mn.x) < eps) n = Vec3(-1.0f, 0.0f, 0.0f);
    else if (std::fabs(p.x - mx.x) < eps) n = Vec3(1.0f, 0.0f, 0.0f);
    else if (std::fabs(p.y - mn.y) < eps) n = Vec3(0.0f, -1.0f, 0.0f);
    else if (std::fabs(p.y - mx.y) < eps) n = Vec3(0.0f, 1.0f, 0.0f);
    else if (std::fabs(p.z - mn.z) < eps) n = Vec3(0.0f, 0.0f, -1.0f);
    else n = Vec3(0.0f, 0.0f, 1.0f);

    out.hit = true; out.t = t; out.point = p; out.normal = n;
    return true;
}

// 按图元类型分派到 gfx 的精确求交（用 tMin=0.001 对齐框架's `t > 0.001f` 口径）
bool intersectPrim(const Ray& ray, const Prim& prim, HitRecord& out) {
    switch (prim.kind) {
        case Prim::Plane:  return gfx::intersectPlane(ray, prim.p0, prim.p1, out, 0.001f);
        case Prim::Sphere: return gfx::intersectSphere(ray, prim.p0, prim.radius, out, 0.001f);
        case Prim::Box:    return intersectBox(ray, prim.p0, prim.p1, out);
    }
    return false;
}

// 各图元的 AABB：平面为无限大 ⇒ 用极大盒（与框架把无界物体塞进 ±1e18 盒一致，保证永不被剪掉）。
constexpr float kHuge = 1e18f;
Aabb computeBox(const Prim& prim) {
    switch (prim.kind) {
        case Prim::Plane:
            return Aabb{Vec3(-kHuge, -kHuge, -kHuge), Vec3(kHuge, kHuge, kHuge)};
        case Prim::Sphere:
            return Aabb{prim.p0 - Vec3(prim.radius, prim.radius, prim.radius),
                        prim.p0 + Vec3(prim.radius, prim.radius, prim.radius)};
        case Prim::Box:
            return Aabb{compMin(prim.p0, prim.p1), compMax(prim.p0, prim.p1)};
    }
    return Aabb{};
}

// 用 BVH 求最近命中；visitor 里按索引做精确求交并回传材质。
bool hitSceneBvh(const Ray& ray, SurfaceHit& out, const std::vector<Prim>& prims, const Bvh& bvh) {
    HitRecord best;
    const Material* bestMat = nullptr;
    bool found = false;

    float bestT = 0.0f;
    bvh.closestHit(ray, [&](const Ray& r, int i, float& outT) {
        HitRecord h;
        if (!intersectPrim(r, prims[static_cast<std::size_t>(i)], h)) return false;
        outT = h.t;
        if (!found || h.t < best.t) { best = h; bestMat = &prims[static_cast<std::size_t>(i)].material; found = true; }
        return true;
    }, bestT);

    if (!found) return false;
    out.geometry = best;
    out.material = bestMat;
    return true;
}

void printUsage() {
    std::cout <<
        "用法：\n"
        "  Hw3SceneTrace [--output <out.png>]\n"
        "  Hw3SceneTrace --help\n"
        "\n"
        "  --output <file>   结果 PNG 路径（默认 output/hw3_gfx_result.png；父目录会自动创建）\n"
        "  --help            显示本帮助\n"
        "\n"
        "  分辨率 800x600、maxDepth=5、每像素 1 采样、关闭伽马均固定为框架原始口径。\n";
}

} // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    SetConsoleOutputCP(65001);   // 控制台按 UTF-8 解释输出 ⇒ 中文不乱码（双击运行同样是 UTF-8 代码页）
#endif
    std::string outputPath = "output/hw3_gfx_result.png";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { printUsage(); return 0; }
        if (arg == "--output" && i + 1 < argc) { outputPath = argv[++i]; }
        else { std::cerr << "错误：无法识别的参数 " << arg << "（用 --help 查看用法）\n"; return 2; }
    }

    // ---- 分辨率 / 递归深度：与原始 main.cpp 完全一致 ----
    const int width = 800;
    const int height = 600;
    const int maxDepth = 5;

    // ================= 场景：逐项复刻 main.cpp.bak =================
    std::vector<Prim> prims;

    // --- 房间：6 个无限平面 ---
    {
        Material m = frameworkDefault();
        m.diffuse = Vec3(0.3f, 0.5f, 0.2f); m.shininess = 16.0f;   // 地面
        prims.push_back(Prim{Prim::Plane, Vec3(0.0f, -5.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), 0.0f, m, {}});
    }
    {
        Material m = frameworkDefault();
        m.diffuse = Vec3(0.5f, 0.5f, 0.5f);                        // 天花板
        prims.push_back(Prim{Prim::Plane, Vec3(0.0f, 15.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 0.0f, m, {}});
    }
    {
        Material m = frameworkDefault();
        m.diffuse = Vec3(0.7f, 0.3f, 0.3f);                        // 左墙（红）
        prims.push_back(Prim{Prim::Plane, Vec3(-15.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), 0.0f, m, {}});
    }
    {
        Material m = frameworkDefault();
        m.diffuse = Vec3(0.3f, 0.5f, 0.7f);                        // 右墙（蓝）
        prims.push_back(Prim{Prim::Plane, Vec3(15.0f, 0.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), 0.0f, m, {}});
    }
    {
        Material m = frameworkDefault();
        m.diffuse = Vec3(0.6f, 0.1f, 0.3f);                        // 前墙
        prims.push_back(Prim{Prim::Plane, Vec3(0.0f, 0.0f, 20.0f), Vec3(0.0f, 0.0f, -1.0f), 0.0f, m, {}});
    }
    {
        Material m = frameworkDefault();
        m.diffuse = Vec3(0.6f, 0.6f, 0.1f);                        // 后墙
        prims.push_back(Prim{Prim::Plane, Vec3(0.0f, 0.0f, -20.0f), Vec3(0.0f, 0.0f, 1.0f), 0.0f, m, {}});
    }

    // --- 玻璃球（中间）---
    {
        Material m = frameworkDefault();
        m.diffuse = Vec3(0.1f, 0.1f, 0.1f);
        m.specular = Vec3(1.0f, 1.0f, 1.0f);
        m.transparency = 0.8f;
        m.reflectivity = 0.2f;
        m.refractiveIndex = 1.5f;
        m.shininess = 128.0f;
        prims.push_back(Prim{Prim::Sphere, Vec3(0.0f, 0.0f, 5.0f), {}, 2.0f, m, {}});
    }

    // --- 绿球（左）---
    {
        Material m = frameworkDefault();
        m.diffuse = Vec3(0.2f, 0.8f, 0.2f);
        m.specular = Vec3(0.5f, 0.5f, 0.5f);
        m.shininess = 64.0f;
        m.reflectivity = 0.3f;
        prims.push_back(Prim{Prim::Sphere, Vec3(-6.0f, 0.0f, 8.0f), {}, 2.0f, m, {}});
    }

    // --- 蓝立方（右）---
    {
        Material m = frameworkDefault();
        m.diffuse = Vec3(0.2f, 0.6f, 0.8f);
        m.specular = Vec3(0.4f, 0.4f, 0.4f);
        m.shininess = 32.0f;
        m.reflectivity = 0.2f;
        prims.push_back(Prim{Prim::Box, Vec3(4.0f, -2.0f, 6.0f), Vec3(8.0f, 2.0f, 10.0f), 0.0f, m, {}});
    }

    for (Prim& p : prims) p.box = computeBox(p);

    // ---- BVH：把各物体的 AABB 建 BVH（平面用极大盒，等效于"总是访问"）----
    std::vector<Aabb> boxes;
    boxes.reserve(prims.size());
    for (const Prim& p : prims) boxes.push_back(p.box);
    Bvh bvh;
    bvh.build(boxes);

    // ---- 3 个点光源（与 main.cpp.bak 一致）----
    std::vector<Light> lights;
    lights.push_back(Light{Vec3(0.0f, 14.0f, 5.0f), Vec3(1.0f, 1.0f, 1.0f), 1.0f});   // 顶部主光
    lights.push_back(Light{Vec3(-10.0f, 10.0f, 0.0f), Vec3(0.9f, 0.9f, 0.7f), 0.7f}); // 左侧暖光
    lights.push_back(Light{Vec3(10.0f, 10.0f, 0.0f), Vec3(0.2f, 0.9f, 0.9f), 0.7f});  // 右侧冷光

    // ================= 相机与像素→射线：逐字照抄框架 render_scene =================
    const Vec3 camera_pos(0.0f, 6.0f, -10.0f);
    const Vec3 look_at(0.0f, 0.0f, 10.0f);
    const Vec3 up_dir(0.0f, 1.0f, 0.0f);

    Vec3 forward, right, actual_up;
    gfx::math::normalize(look_at - camera_pos, forward);
    gfx::math::normalize(gfx::math::cross(forward, up_dir), right);
    actual_up = gfx::math::cross(right, forward);

    const float fov = 60.0f * 3.14159265358979323846 / 180.0f;   // 与框架同样是 double 运算后落到 float
    const float aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
    const float scale = std::tan(fov * 0.5f);

    // 框架 render_scene 的像素→方向公式（n=1 时 ox=oy=0.5，即 px=x+0.5）：
    //   pixel_x = (2*(x+0.5)/width - 1) * aspect * scale
    //   pixel_y = (1 - 2*(y+0.5)/height) * scale
    //   dir     = normalize(forward + right*pixel_x + actual_up*pixel_y)
    auto rayForPixel = [&](float px, float py) {
        const float pixel_x = (2.0f * px / static_cast<float>(width) - 1.0f) * aspect_ratio * scale;
        const float pixel_y = (1.0f - 2.0f * py / static_cast<float>(height)) * scale;
        Vec3 dir;
        gfx::math::normalize(forward + right * pixel_x + actual_up * pixel_y, dir);
        return Ray(camera_pos, dir);
    };

    // ---- 相机公式等价性自检：对照 gfx::View（lookAt + setPerspective + pixelToRay）----
    // 期望二者等价；此处抽样打印最大方向偏差，作为"我们确实核对过"的证据。
    double maxDirDev = 0.0;
    {
        View view;
        view.setViewport(width, height);
        view.lookAt(camera_pos, look_at, up_dir);
        view.setPerspective(60.0f * 3.14159265358979323846f / 180.0f, aspect_ratio, 0.1f, 100.0f);
        for (int y = 0; y < height; y += 25) {
            for (int x = 0; x < width; x += 25) {
                const Vec3 d1 = rayForPixel(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f).direction;
                Vec3 o, d2;
                view.pixelToRay(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, o, d2);
                maxDirDev = std::max(maxDirDev, static_cast<double>((d1 - d2).length()));
            }
        }
    }

    // ================= 渲染 =================
    // 场景求交回调：复用同一个 BVH 求交（阴影 / 反射 / 折射都由 RayTracer 内部复用该回调）。
    RayTracer::HitFunction hitFunction = [&](const Ray& ray, SurfaceHit& out) {
        return hitSceneBvh(ray, out, prims, bvh);
    };

    RayTracer tracer(hitFunction, lights);
    tracer.setBackground(Vec3(0.1f, 0.1f, 0.1f));   // 与框架 Scene() 默认背景色一致
    tracer.setSamplesPerAxis(1);                    // 关超采样（与基准图对齐）
    tracer.setGammaCorrect(false);                  // 关伽马（与基准图对齐）

    Canvas canvas(width, height);
    // 渲染进度：与课程框架 render_scene 的做法一致，每 50 行打印一次百分比（末行必打 100%）。
    tracer.render(canvas, rayForPixel, maxDepth, [](int done, int total) {
        if (done % 50 == 0 || done == total) {
            std::cout << "渲染进度: " << (done * 100 / total) << "%" << std::endl;
        }
    });

    std::string error;
    if (!gfx::savePng(canvas, outputPath.c_str(), &error)) {
        std::cerr << "错误：保存 PNG 失败 —— " << error << "\n";
        return 5;
    }

    std::cout << "用 gfx 复刻课程框架（作业三）原始场景并渲染：\n";
    std::cout << "  分辨率      : " << width << "x" << height << "（每像素 1 采样，关闭伽马）\n";
    std::cout << "  递归深度    : " << maxDepth << "（与原始 main.cpp 一致）\n";
    std::cout << "  场景图元    : 6 面 + 玻璃球 + 绿球 + 蓝立方 = " << prims.size() << " 个（全部入 BVH）\n";
    std::cout << "  光源        : 3 个点光源；背景色 (0.1,0.1,0.1)\n";
    std::cout << "  BVH         : 节点 " << bvh.nodeCount() << " / 叶子 " << bvh.leafCount() << "\n";
    std::cout << "  相机公式自检: max|公式方向 − View::pixelToRay| = " << maxDirDev
              << "（抽样 25px 步长；0 表示二者等价）\n";
    std::cout << "  输出        : " << outputPath << "\n";
    if (argc == 1) {
        std::cout << "（未指定参数：已用默认设置渲染，路径相对当前目录 —— 双击运行时即 exe 所在目录）\n";
        pauseIfInteractive();
    }
    return 0;
}
