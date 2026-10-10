// ray_basics.cpp —— 教学示例：光线求交地基（**只用求交，不做任何光照/递归**）
//
// 用途：把 gfx/geom/ray.hpp 与 gfx/geom/bvh.hpp 的纯几何能力端到端用一遍 ——
//       为每个像素从相机射出一条**主光线**，在"球 / 三角形 / 圆柱 / 无限平面"场景里求最近命中，
//       把命中点的**单位法向**直接映射成 RGB（x→R, y→G, z→B），保存为 PNG。
//       全程不含光照模型、不递归：这就是光线跟踪的"地基"（后续阶段再叠光照/反射/折射）。
//
// 权威实现：gfx::intersectSphere / intersectTriangle / intersectCylinder / intersectPlane（ray.hpp）
//           gfx::Bvh::closestHit（bvh.hpp，与图元类型解耦：回调按索引做精确求交）
// 构建：cmake --build build --target tutorial_ray_basics
// 运行：build/tutorials/RayBasics --output tutorials/ray-basics/output/ray-basics.png
//
// 参数：--output <png>（默认 output/ray-basics.png，父目录会自动创建）/ --size WxH / --help

#include <gfx/core/canvas.hpp>
#include <gfx/core/color.hpp>
#include <gfx/geom/geom.hpp>
#include <gfx/render/png.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

using gfx::Aabb;
using gfx::Bvh;
using gfx::Canvas;
using gfx::Color;
using gfx::HitRecord;
using gfx::Ray;
using gfx::rgb;
using gfx::math::Vec3;

const Color kBackground = rgb(12, 14, 22);

struct Options {
    std::string outputPath = "output/ray-basics.png";
    int width = 800;
    int height = 600;
};

void printUsage() {
    std::cout <<
        "用法：\n"
        "  RayBasics [--output <out.png>] [--size WxH]\n"
        "  RayBasics --help\n"
        "\n"
        "  --output <file>   结果 PNG 路径（默认 output/ray-basics.png；父目录会自动创建）\n"
        "  --size WxH        画布尺寸（默认 800x600）\n"
        "  --help            显示本帮助\n";
}

bool parseArgs(int argc, char** argv, Options& opt, int& exitCode) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { printUsage(); exitCode = 0; return false; }
        if (arg == "--output" && i + 1 < argc) { opt.outputPath = argv[++i]; }
        else if (arg == "--size" && i + 1 < argc) {
            const std::string size = argv[++i];
            const std::size_t sep = size.find('x');
            if (sep == std::string::npos) {
                std::cerr << "错误：--size 需要 WxH 形式，例如 800x600\n";
                exitCode = 2;
                return false;
            }
            opt.width = std::atoi(size.substr(0, sep).c_str());
            opt.height = std::atoi(size.substr(sep + 1).c_str());
            if (opt.width < 64 || opt.height < 64) {
                std::cerr << "错误：画布尺寸过小（至少 64x64）\n";
                exitCode = 2;
                return false;
            }
        } else {
            std::cerr << "错误：无法识别的参数 " << arg << "（用 --help 查看用法）\n";
            exitCode = 2;
            return false;
        }
    }
    return true;
}

// 场景图元（用类型标签 + 参数 + 预计算 AABB 表示；BVH 只吃 AABB，精确求交在回调里按索引分派）
struct Prim {
    enum Kind { Sphere, Triangle, Cylinder } kind = Sphere;
    Vec3 p0, p1, p2;          // 球：p0=中心；三角形：p0/p1/p2=三顶点；圆柱：p0=轴心点、p1=轴方向
    float radius = 0.0f;      // 球半径 / 圆柱半径
    float halfHeight = 0.0f;  // 圆柱半高
    Aabb box;
};

// 按图元类型分派到 ray.hpp 的精确求交（与 BVH 解耦的核心：回调自己知道 index 对应谁）
bool intersectPrim(const Ray& ray, const Prim& prim, HitRecord& out) {
    switch (prim.kind) {
        case Prim::Sphere:   return gfx::intersectSphere(ray, prim.p0, prim.radius, out);
        case Prim::Triangle: return gfx::intersectTriangle(ray, prim.p0, prim.p1, prim.p2, out);
        case Prim::Cylinder: return gfx::intersectCylinder(ray, prim.p0, prim.p1, prim.radius, prim.halfHeight, out);
    }
    return false;
}

Vec3 compMin(const Vec3& a, const Vec3& b) {
    return Vec3(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
}
Vec3 compMax(const Vec3& a, const Vec3& b) {
    return Vec3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
}

// 由各图元几何计算 AABB（圆柱用"轴段端点 ± 半径"的保守盒，合法且简单）
Aabb computeBox(const Prim& prim) {
    switch (prim.kind) {
        case Prim::Sphere:
            return Aabb{prim.p0 - Vec3(prim.radius, prim.radius, prim.radius),
                        prim.p0 + Vec3(prim.radius, prim.radius, prim.radius)};
        case Prim::Triangle:
            return Aabb{compMin(compMin(prim.p0, prim.p1), prim.p2),
                        compMax(compMax(prim.p0, prim.p1), prim.p2)};
        case Prim::Cylinder: {
            Vec3 a;
            gfx::math::normalize(prim.p1, a);
            const Vec3 e0 = prim.p0 - a * prim.halfHeight;
            const Vec3 e1 = prim.p0 + a * prim.halfHeight;
            const Vec3 r(prim.radius, prim.radius, prim.radius);
            return Aabb{compMin(e0, e1) - r, compMax(e0, e1) + r};
        }
    }
    return Aabb{};
}

// 法向 → RGB：n 单位时各分量 ∈ [-1,1]，线性映射到 [0,255]
Color shadeNormal(const Vec3& n) {
    const auto to8 = [](float v) -> std::uint8_t {
        const float s = (v * 0.5f + 0.5f) * 255.0f;
        return static_cast<std::uint8_t>(std::lround(std::min(255.0f, std::max(0.0f, s))));
    };
    return rgb(to8(n.x), to8(n.y), to8(n.z));
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    int exitCode = 0;
    if (!parseArgs(argc, argv, opt, exitCode)) return exitCode;

    // ---- 场景：一个地面 + 三个球 + 一个三角形背板 + 一根任意轴（Y 轴）圆柱 ----
    std::vector<Prim> prims;
    prims.push_back(Prim{Prim::Sphere, Vec3(-1.15f, -0.25f, -1.10f), {}, {}, 0.72f, 0.0f, {}});
    prims.push_back(Prim{Prim::Sphere, Vec3(0.35f, -0.35f, -1.90f), {}, {}, 0.62f, 0.0f, {}});
    prims.push_back(Prim{Prim::Sphere, Vec3(-0.20f, 1.05f, -2.20f), {}, {}, 0.38f, 0.0f, {}});
    prims.push_back(Prim{Prim::Triangle, Vec3(-2.2f, -1.0f, -3.6f), Vec3(2.2f, -1.0f, -3.6f),
                         Vec3(0.0f, 1.6f, -3.6f), 0.0f, 0.0f, {}});
    prims.push_back(Prim{Prim::Cylinder, Vec3(1.45f, -1.0f, -1.0f), Vec3(0.0f, 1.0f, 0.0f), {},
                         0.5f, 1.0f, {}});
    for (Prim& p : prims) p.box = computeBox(p);

    // ---- BVH：只吃 AABB；精确求交由回调按索引完成（与图元类型解耦） ----
    std::vector<Aabb> boxes;
    boxes.reserve(prims.size());
    for (const Prim& p : prims) boxes.push_back(p.box);
    Bvh bvh;
    bvh.build(boxes);

    // 无限平面不放进 BVH（无有限 AABB），单独求交后与 BVH 结果取最近
    const Vec3 planePoint(0.0f, -1.0f, 0.0f);
    const Vec3 planeNormal(0.0f, 1.0f, 0.0f);

    // ---- 相机：右手系针孔模型（eye → target，up 为世界 Y） ----
    const Vec3 eye(0.0f, 0.75f, 3.4f);
    const Vec3 target(0.0f, 0.0f, -1.0f);
    const float aspect = static_cast<float>(opt.width) / static_cast<float>(opt.height);
    const float tanHalfFov = std::tan(0.5f * 45.0f * 3.14159265358979f / 180.0f);

    Vec3 forward, right, upv;
    gfx::math::normalize(target - eye, forward);
    gfx::math::normalize(gfx::math::cross(forward, Vec3(0.0f, 1.0f, 0.0f)), right);
    upv = gfx::math::cross(right, forward);

    Canvas canvas(opt.width, opt.height);
    canvas.clear(kBackground);

    long long hitPixels = 0;
    long long totalCallbacks = 0;   // BVH 回调（精确求交）总次数
    long long totalVisits = 0;      // BVH 遍历节点总次数

    for (int y = 0; y < opt.height; ++y) {
        for (int x = 0; x < opt.width; ++x) {
            const float nx = (static_cast<float>(x) + 0.5f) / static_cast<float>(opt.width) * 2.0f - 1.0f;
            const float ny = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(opt.height) * 2.0f;
            Vec3 dir;
            gfx::math::normalize(forward + right * (nx * aspect * tanHalfFov) + upv * (ny * tanHalfFov), dir);
            const Ray ray(eye, dir);

            bool found = false;
            HitRecord best;
            HitRecord hp;
            if (gfx::intersectPlane(ray, planePoint, planeNormal, hp)) { best = hp; found = true; }

            HitRecord pb;
            float bvhT = 0.0f;
            long long calls = 0;
            const bool bvhFound = bvh.closestHit(ray, [&](const Ray& r, int i, float& t) {
                ++calls;
                HitRecord h;
                if (!intersectPrim(r, prims[static_cast<std::size_t>(i)], h)) return false;
                t = h.t;
                if (!pb.hit || h.t < pb.t) pb = h;
                return true;
            }, bvhT);
            totalCallbacks += calls;
            totalVisits += static_cast<long long>(bvh.lastVisitCount());

            if (bvhFound && (!found || pb.t < best.t)) { best = pb; found = true; }

            if (found) {
                canvas.putPixel(x, y, shadeNormal(best.normal));
                ++hitPixels;
            }
        }
    }

    // ---- 保存 PNG（父目录不存在则创建） ----
    std::error_code ec;
    const std::filesystem::path outPath(opt.outputPath);
    if (outPath.has_parent_path()) std::filesystem::create_directories(outPath.parent_path(), ec);

    std::string error;
    if (!gfx::savePng(canvas, opt.outputPath.c_str(), &error)) {
        std::cerr << "错误：保存 PNG 失败 —— " << error << "\n";
        return 5;
    }

    // ---- 控制台统计（可复现） ----
    const double pixels = static_cast<double>(opt.width) * static_cast<double>(opt.height);
    std::cout << "光线求交 · 法向可视化（无光照）\n";
    std::cout << "  分辨率        : " << opt.width << "x" << opt.height << "（主光线 " << static_cast<long long>(pixels) << " 条）\n";
    std::cout << "  场景图元      : " << prims.size() << "（球 x3 + 三角形 x1 + 圆柱 x1）+ 无限平面 x1\n";
    std::cout << "  BVH           : 节点 " << bvh.nodeCount() << " / 叶子 " << bvh.leafCount() << "\n";
    std::cout << "  命中像素      : " << hitPixels << "（占 "
              << static_cast<int>(100.0 * static_cast<double>(hitPixels) / pixels) << "%）\n";
    std::cout << "  每像素平均    : BVH 回调 " << (static_cast<double>(totalCallbacks) / pixels)
              << " 次 / 节点访问 " << (static_cast<double>(totalVisits) / pixels) << " 次"
              << "（暴力法需 " << prims.size() << " 次/像素）\n";
    std::cout << "  输出          : " << opt.outputPath << "\n";
    return 0;
}
