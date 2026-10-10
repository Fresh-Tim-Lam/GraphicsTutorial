// normal_mapping.cpp —— 法线贴图演示（光线跟踪路径）
//
// 用途：演示"不增加几何量、只扰动表面法向"的效果。场景里有三颗**完全相同**的球：
//         左：无法线贴图（对照组）   中：砖缝法线贴图   右：波纹法线贴图
//       地面是一整块平面，铺棋格法线贴图（展示平铺）。
//       三颗球/地面共用同一套材质参数，所以画面差异**只来自法线贴图**。
//
// 用法：build/tutorials/NormalMapping [--output <out.png>] [--size WxH] [--samples n] [--depth d]
//                                     [--strength s] [--no-map] [--compare] [--help]
//   --strength s  凹凸强度（默认 1.0；0 = 贴图退化成平整法向，等价于不用贴图）
//   --no-map      全部关闭法线贴图（做 A/B 对照）
//   --compare     额外渲染一张"关闭法线贴图"的图并统计差异像素（默认关闭，约多花一倍时间）
//
// 实现要点（详见 tutorials/normal-mapping/README.md）：
//   - 命中后由形状参数化算出 (u,v) 与切空间 TBN（球面/平面都是解析解）；
//   - 采样法线贴图 → 解码成切线空间法向 → 用 TBN 变换到世界空间；
//   - 结果写进 `SurfaceHit::shadingNormal`：**只有光照用它**，几何法向仍负责反射/折射/自交偏移。
//
// 关联：src/include/gfx/render/texture.hpp、normalmap.hpp、raytracer.hpp

#include <gfx/core/canvas.hpp>
#include <gfx/core/color.hpp>
#include <gfx/geom/ray.hpp>
#include <gfx/render/lighting.hpp>
#include <gfx/render/normalmap.hpp>
#include <gfx/render/png.hpp>
#include <gfx/render/raytracer.hpp>
#include <gfx/render/texture.hpp>
#include <gfx/render/view.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX      // 避免 windows.h 的 min/max 宏破坏 std::min / std::max
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>   // SetConsoleOutputCP（控制台按 UTF-8 输出，避免中文乱码）
#include <io.h>        // _isatty / _fileno（双击运行时判断 stdin 是否为终端）
#endif

namespace {

using gfx::Canvas;
using gfx::HitRecord;
using gfx::Light;
using gfx::Material;
using gfx::Ray;
using gfx::RayTracer;
using gfx::SurfaceHit;
using gfx::TangentBasis;
using gfx::Texture;
using gfx::View;
using gfx::math::Vec3;

constexpr float kPi = 3.14159265358979323846f;

// ---- 场景图元（本演示只有"球"与"平面"两种，且都带可选法线贴图）----

struct SphereObj {
    Vec3 center;
    float radius = 1.0f;
    Material material;
    const Texture* normalMap = nullptr;   // nullptr ⇒ 不用法线贴图（对照）
    float uvScale = 1.0f;                 // uv 平铺倍数（越大图案越密）
};

struct PlaneObj {
    Vec3 point;
    Vec3 normal{0.0f, 1.0f, 0.0f};
    Material material;
    const Texture* normalMap = nullptr;
    float tileSize = 2.0f;                // 一个 uv 单位对应多少世界单位
};

struct Scene {
    std::vector<SphereObj> spheres;
    PlaneObj floor;
    std::vector<Light> lights;
    Vec3 background{0.05f, 0.06f, 0.09f};
};

Material makeMaterial(Vec3 diffuse, Vec3 specular, float shininess,
                      float reflectivity = 0.0f, float transparency = 0.0f, float refractiveIndex = 1.0f) {
    Material m;
    m.diffuse = diffuse;
    m.specular = specular;
    m.shininess = shininess;
    m.reflectivity = reflectivity;
    m.transparency = transparency;
    m.refractiveIndex = refractiveIndex;
    return m;
}

// 砖缝 / 波纹 / 棋格三张程序化法线贴图（强度由外部统一缩放）
struct Maps {
    Texture brick;
    Texture wave;
    Texture checker;
};

Maps makeMaps(int size, float strength) {
    Maps maps;
    maps.brick = gfx::makeNormalMap(size, [](float u, float v) {
        return gfx::brickHeight(u, v, /*rows=*/5, /*columns=*/4, /*mortarWidth=*/0.05f, /*amplitude=*/0.24f);
    }, strength);
    maps.wave = gfx::makeNormalMap(size, [](float u, float v) {
        return gfx::waveHeight(u, v, /*frequency=*/5.0f, /*amplitude=*/0.22f);
    }, strength);
    maps.checker = gfx::makeNormalMap(size, [](float u, float v) {
        return gfx::checkerHeight(u, v, /*cells=*/6, /*amplitude=*/0.22f);
    }, strength);
    return maps;
}

Scene makeScene(const Maps& maps, bool useMaps) {
    Scene scene;

    // 三颗球共用同一套材质 ⇒ 画面差异只来自法线贴图
    const Material sphereMat = makeMaterial(Vec3(0.55f, 0.55f, 0.58f), Vec3(0.85f, 0.85f, 0.85f), 96.0f,
                                            /*reflectivity=*/0.25f);
    const float y = -0.25f, z = -0.20f, r = 0.75f;
    const float xs[3] = {-1.45f, 0.0f, 1.45f};

    SphereObj plain;
    plain.center = Vec3(xs[0], y, z); plain.radius = r; plain.material = sphereMat;
    scene.spheres.push_back(plain);

    SphereObj brick;
    brick.center = Vec3(xs[1], y, z); brick.radius = r; brick.material = sphereMat;
    brick.normalMap = useMaps ? &maps.brick : nullptr; brick.uvScale = 1.0f;
    scene.spheres.push_back(brick);

    SphereObj wave;
    wave.center = Vec3(xs[2], y, z); wave.radius = r; wave.material = sphereMat;
    wave.normalMap = useMaps ? &maps.wave : nullptr; wave.uvScale = 1.5f;
    scene.spheres.push_back(wave);

    // 地面：一整块平面，铺棋格贴图
    scene.floor.point = Vec3(0.0f, -1.0f, 0.0f);
    scene.floor.normal = Vec3(0.0f, 1.0f, 0.0f);
    scene.floor.material = makeMaterial(Vec3(0.34f, 0.36f, 0.40f), Vec3(0.45f, 0.45f, 0.45f), 64.0f, 0.10f);
    scene.floor.normalMap = useMaps ? &maps.checker : nullptr;
    scene.floor.tileSize = 1.6f;

    scene.lights.push_back(Light{Vec3(3.0f, 5.0f, 4.0f), Vec3(1.00f, 0.96f, 0.90f), 1.15f});   // 主光
    scene.lights.push_back(Light{Vec3(-3.5f, 3.0f, 2.5f), Vec3(1.00f, 0.72f, 0.50f), 0.55f});  // 暖补光
    return scene;
}

// 命中时按形状算出 uv 与切空间，并把扰动后的法向写进 shadingNormal
bool hitScene(const Ray& ray, SurfaceHit& out, const Scene& scene) {
    HitRecord best;
    const Material* bestMaterial = nullptr;
    const Texture* bestMap = nullptr;
    float bestUvScale = 1.0f;
    int bestKind = -1;            // 0 = 球，1 = 平面
    Vec3 bestCenterOrPoint;
    int bestSphereIndex = -1;

    // 球
    for (std::size_t i = 0; i < scene.spheres.size(); ++i) {
        const SphereObj& s = scene.spheres[i];
        HitRecord h;
        if (gfx::intersectSphere(ray, s.center, s.radius, h) && (!best.hit || h.t < best.t)) {
            best = h;
            bestMaterial = &s.material;
            bestMap = s.normalMap;
            bestUvScale = s.uvScale;
            bestKind = 0;
            bestSphereIndex = static_cast<int>(i);
        }
    }
    // 平面（无限地面）
    HitRecord hp;
    if (gfx::intersectPlane(ray, scene.floor.point, scene.floor.normal, hp) && (!best.hit || hp.t < best.t)) {
        best = hp;
        bestMaterial = &scene.floor.material;
        bestMap = scene.floor.normalMap;
        bestKind = 1;
        bestCenterOrPoint = scene.floor.point;
    }

    if (!best.hit) return false;
    out.geometry = best;
    out.material = bestMaterial;

    if (bestMap == nullptr) return true;   // 对照组：shadingNormal 保持零向量 ⇒ 用几何法向

    // ---- 算法向与切空间 ----
    TangentBasis basis;
    float u = 0.0f, v = 0.0f;
    if (bestKind == 0) {
        const SphereObj& s = scene.spheres[static_cast<std::size_t>(bestSphereIndex)];
        const Vec3 d = best.point - s.center;
        const Vec3 unit = d * (1.0f / s.radius);
        u = 0.5f + std::atan2(unit.z, unit.x) / (2.0f * kPi);
        v = std::acos(std::max(-1.0f, std::min(1.0f, unit.y))) / kPi;
        basis = gfx::sphereTangentBasis(best.point, s.center);
        u *= s.uvScale;
        v *= s.uvScale;
    } else {
        basis = gfx::planeTangentBasis(scene.floor.normal);
        const Vec3 rel = best.point - scene.floor.point;
        u = gfx::math::dot(rel, basis.tangent) / scene.floor.tileSize;
        v = gfx::math::dot(rel, basis.bitangent) / scene.floor.tileSize;
    }

    const Vec3 nTangent = gfx::decodeNormal(bestMap->sample(u, v));
    out.shadingNormal = gfx::perturbNormal(basis, nTangent);
    return true;
}

struct Options {
    std::string outputPath = "output/normal-mapping.png";
    std::string comparePath = "output/normal-mapping-no-map.png";
    int width = 800;
    int height = 600;
    int samplesPerAxis = 3;
    int maxDepth = 6;
    float strength = 1.0f;
    bool noMap = false;
    bool compare = false;
};

void printUsage() {
    std::cout <<
        "用法：\n"
        "  NormalMapping [--output <out.png>] [--size WxH] [--samples n] [--depth d]\n"
        "                [--strength s] [--no-map] [--compare] [--help]\n"
        "\n"
        "  --output <file>  结果 PNG 路径（默认 output/normal-mapping.png；父目录自动创建）\n"
        "  --size WxH       画布尺寸（默认 800x600）\n"
        "  --samples n      每像素 n×n 超采样（默认 3，n=1 关闭）\n"
        "  --depth d        递归深度（默认 6）\n"
        "  --strength s     凹凸强度（默认 1.0；0 = 贴图退化成平整法向）\n"
        "  --no-map         全部关闭法线贴图（A/B 对照）\n"
        "  --compare        额外渲染一张关闭贴图的图并统计差异像素（默认关闭）\n"
        "  --help           显示本帮助\n";
}

bool parseArgs(int argc, char** argv, Options& opt, int& exitCode) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { printUsage(); exitCode = 0; return false; }
        if (arg == "--output" && i + 1 < argc) { opt.outputPath = argv[++i]; }
        else if (arg == "--no-map") { opt.noMap = true; }
        else if (arg == "--compare") { opt.compare = true; }
        else if (arg == "--strength" && i + 1 < argc) { opt.strength = static_cast<float>(std::atof(argv[++i])); }
        else if (arg == "--samples" && i + 1 < argc) { opt.samplesPerAxis = std::atoi(argv[++i]); }
        else if (arg == "--depth" && i + 1 < argc) { opt.maxDepth = std::atoi(argv[++i]); }
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
        } else {
            std::cerr << "错误：无法识别的参数 " << arg << "（用 --help 查看用法）\n";
            exitCode = 2;
            return false;
        }
    }
    if (opt.width < 32) opt.width = 32;
    if (opt.height < 32) opt.height = 32;
    if (opt.samplesPerAxis < 1) opt.samplesPerAxis = 1;
    if (opt.maxDepth < 1) opt.maxDepth = 1;
    if (opt.strength < 0.0f) opt.strength = 0.0f;
    return true;
}

// 双击运行（命令行无参数）时渲染完暂停一下，避免控制台一闪而过
void pauseIfInteractive() {
#if defined(_WIN32)
    if (_isatty(_fileno(stdin))) {
        std::cout << "\n按回车键退出 ..." << std::flush;
        std::cin.get();
    }
#endif
}

} // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    SetConsoleOutputCP(65001);   // 控制台按 UTF-8 输出，避免中文乱码
#endif
    Options opt;
    int exitCode = 0;
    if (!parseArgs(argc, argv, opt, exitCode)) return exitCode;

    const bool useMaps = !opt.noMap;
    const Maps maps = makeMaps(256, opt.strength);
    const Scene scene = makeScene(maps, useMaps);

    // 相机
    View view;
    view.setViewport(opt.width, opt.height);
    view.lookAt(Vec3(0.0f, 1.45f, 5.05f), Vec3(0.0f, -0.10f, 0.10f), Vec3(0.0f, 1.0f, 0.0f));
    view.setPerspective(40.0f * kPi / 180.0f,
                        static_cast<float>(opt.width) / static_cast<float>(opt.height), 0.05f, 200.0f);

    auto rayForPixel = [&](float px, float py) {
        Vec3 origin, direction;
        view.pixelToRay(px, py, origin, direction);
        return Ray(origin, direction);
    };
    RayTracer::HitFunction hit = [&](const Ray& ray, SurfaceHit& out) { return hitScene(ray, out, scene); };

    RayTracer tracer(hit, scene.lights);
    tracer.setBackground(scene.background);
    tracer.setSamplesPerAxis(opt.samplesPerAxis);

    std::cout << "法线贴图演示（光线跟踪路径）\n"
                 "  分辨率        : " << opt.width << "x" << opt.height
              << "（每像素 " << opt.samplesPerAxis << "x" << opt.samplesPerAxis << " 超采样）\n"
              << "  递归深度      : " << opt.maxDepth << "\n"
              << "  凹凸强度      : " << opt.strength << (useMaps ? "" : "（已用 --no-map 关闭全部贴图）") << "\n"
              << "  场景          : 左=对照球（无贴图）  中=砖缝贴图球  右=波纹贴图球  地面=棋格贴图平面\n"
              << "  贴图生成      : 程序化（高度场 → 中心差分求斜率 → 切线空间法向）\n";

    Canvas canvas(opt.width, opt.height);
    const auto t0 = std::chrono::high_resolution_clock::now();
    tracer.render(canvas, rayForPixel, opt.maxDepth, [](int done, int total) {
        if (done % 100 == 0 || done == total) std::cout << "  渲染进度: " << (done * 100 / total) << "%\n";
    });
    const double seconds = std::chrono::duration<double>(
        std::chrono::high_resolution_clock::now() - t0).count();

    std::string error;
    if (!gfx::savePng(canvas, opt.outputPath.c_str(), &error)) {
        std::cerr << "错误：保存 PNG 失败 —— " << error << "\n";
        return 4;
    }
    std::cout << "  渲染耗时      : " << seconds * 1000.0 << " ms\n"
                 "  输出          : " << opt.outputPath << "\n";

    // A/B 对照：再渲一张"关掉全部法线贴图"的，统计差异像素
    if (opt.compare) {
        const Scene plainScene = makeScene(maps, /*useMaps=*/false);
        RayTracer plainTracer(
            [&](const Ray& ray, SurfaceHit& out) { return hitScene(ray, out, plainScene); }, plainScene.lights);
        plainTracer.setBackground(plainScene.background);
        plainTracer.setSamplesPerAxis(opt.samplesPerAxis);
        Canvas plainCanvas(opt.width, opt.height);
        plainTracer.render(plainCanvas, rayForPixel, opt.maxDepth);

        long long diff = 0;
        long long maxDiff = 0;
        long long sumDiff = 0;
        for (int y = 0; y < opt.height; ++y) {
            for (int x = 0; x < opt.width; ++x) {
                const gfx::Color a = canvas.pixelAt(x, y);
                const gfx::Color b = plainCanvas.pixelAt(x, y);
                const int d = std::max({std::abs(int(a.r) - int(b.r)), std::abs(int(a.g) - int(b.g)),
                                        std::abs(int(a.b) - int(b.b))});
                if (d > 0) ++diff;
                if (d > maxDiff) maxDiff = d;
                sumDiff += d;
            }
        }
        const long long pixels = static_cast<long long>(opt.width) * opt.height;
        std::cout << "  对照（关闭法线贴图）: "
                  << diff << " / " << pixels << " 个像素不同（占 "
                  << (100.0 * static_cast<double>(diff) / static_cast<double>(pixels)) << "%）"
                     "，最大通道差 " << maxDiff
                  << "，平均差 " << (static_cast<double>(sumDiff) / static_cast<double>(pixels)) << "\n";
        std::string plainError;
        gfx::savePng(plainCanvas, opt.comparePath.c_str(), &plainError);
        std::cout << "  对照图输出    : " << opt.comparePath << "\n";
    }

    if (argc == 1) pauseIfInteractive();
    return 0;
}
