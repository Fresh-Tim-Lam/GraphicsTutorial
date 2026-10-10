// shaded_raster.cpp —— 光栅化着色管线演示：带纹理 + 法线贴图的立方体 / 球 / 地面
//
// 用途：演示 `gfx/render/shading.hpp` 的能力 —— 顶点属性（位置/法向/切线/uv）经
//       **投影 + 近平面裁剪 + 透视校正插值**后，逐像素调用片元着色器：
//         采样法线贴图 → 切线空间扰动法向 → 喂给 shadeLocal（Blinn-Phong）→ 逐像素颜色。
//       这条路上的两个部件都是复用现成的：`texture.hpp`（采样）与 `normalmap.hpp`（TBN + 高度场）。
//
// 与光线跟踪演示（tutorials/normal-mapping）的区别：
//       那边是"每像素一条光线求交"，这边是"三角形光栅化 + 属性插值"，
//       也就是 GPU 里真正跑的那条路；同一份 Texture / TangentBasis / perturbNormal 两边通用。
//
// 用法：build/tutorials/ShadedRaster [--output <out.png>] [--size WxH] [--strength s] [--help]
//   --strength s  凹凸强度（默认 1.0；0 = 贴图退化成平整法向）
//
// 关联：src/include/gfx/render/shading.hpp、texture.hpp、normalmap.hpp、lighting.hpp

#include <gfx/core/canvas.hpp>
#include <gfx/core/color.hpp>
#include <gfx/render/depth.hpp>
#include <gfx/render/lighting.hpp>
#include <gfx/render/normalmap.hpp>
#include <gfx/render/png.hpp>
#include <gfx/render/shading.hpp>
#include <gfx/render/texture.hpp>
#include <gfx/render/view.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>   // SetConsoleOutputCP（控制台 UTF-8，中文不乱码）
#include <io.h>
#endif

namespace {

using gfx::Canvas;
using gfx::Color;
using gfx::DepthBuffer;
using gfx::Fragment;
using gfx::Light;
using gfx::Material;
using gfx::ShadingTriangle;
using gfx::TangentBasis;
using gfx::Texture;
using gfx::VertexAttributes;
using gfx::View;
using gfx::math::Vec2;
using gfx::math::Vec3;

constexpr float kPi = 3.14159265358979323846f;

// 每个"部件"= 一批三角形 + 材质 + 可选法线贴图
struct Part {
    std::vector<ShadingTriangle> triangles;
    Material material;
    const Texture* normalMap = nullptr;
};

VertexAttributes attribute(const Vec3& p, const Vec3& n, const Vec3& t, const Vec2& uv) {
    VertexAttributes a;
    a.position = p;
    a.normal = n;
    a.tangent = t;
    a.uv = uv;
    return a;
}

// 一块四边形（两个三角形）：origin 为左下角，edgeU/edgeV 是两条边向量，uv 按 uvScale 平铺
void addQuad(Part& part, const Vec3& origin, const Vec3& edgeU, const Vec3& edgeV,
             const Vec3& normal, float uvScale) {
    Vec3 tangent;
    if (!gfx::math::normalize(edgeU, tangent)) return;

    const Vec3 p00 = origin;
    const Vec3 p10 = origin + edgeU;
    const Vec3 p11 = origin + edgeU + edgeV;
    const Vec3 p01 = origin + edgeV;
    const Vec2 uv00(0.0f, 0.0f);
    const Vec2 uv10(uvScale, 0.0f);
    const Vec2 uv11(uvScale, uvScale);
    const Vec2 uv01(0.0f, uvScale);

    part.triangles.push_back(ShadingTriangle{attribute(p00, normal, tangent, uv00),
                                             attribute(p10, normal, tangent, uv10),
                                             attribute(p11, normal, tangent, uv11)});
    part.triangles.push_back(ShadingTriangle{attribute(p00, normal, tangent, uv00),
                                             attribute(p11, normal, tangent, uv11),
                                             attribute(p01, normal, tangent, uv01)});
}

// 立方体：六个面各铺一张贴图，每面自带法向与切线
Part makeCube(const Vec3& center, float half, const Material& material, const Texture* map, float uvScale) {
    Part part;
    part.material = material;
    part.normalMap = map;
    // 面法向 + 面内两条边（沿 +u / +v）
    const Vec3 normals[6] = {Vec3(0, 0, 1), Vec3(0, 0, -1), Vec3(1, 0, 0),
                             Vec3(-1, 0, 0), Vec3(0, 1, 0), Vec3(0, -1, 0)};
    for (int i = 0; i < 6; ++i) {
        const Vec3 n = normals[i];
        Vec3 edgeU;
        if (!gfx::math::normalize(gfx::math::cross(Vec3(0.0f, 1.0f, 0.0f), n), edgeU)) {
            edgeU = Vec3(1.0f, 0.0f, 0.0f);   // 上/下面退化时换参考轴
        }
        const Vec3 edgeV = gfx::math::cross(n, edgeU);
        const Vec3 origin = center + n * half - edgeU * half - edgeV * half;
        addQuad(part, origin, edgeU * (2.0f * half), edgeV * (2.0f * half), n, uvScale);
    }
    return part;
}

// 球：经纬网格；法向 = 径向，切线 = 经度方向（与 normalmap.hpp 的球面切空间口径一致）
Part makeSphere(const Vec3& center, float radius, int rings, int segments,
                const Material& material, const Texture* map, float uvScale) {
    Part part;
    part.material = material;
    part.normalMap = map;
    for (int r = 0; r < rings; ++r) {
        const float v0 = static_cast<float>(r) / static_cast<float>(rings);
        const float v1 = static_cast<float>(r + 1) / static_cast<float>(rings);
        for (int s = 0; s < segments; ++s) {
            const float u0 = static_cast<float>(s) / static_cast<float>(segments);
            const float u1 = static_cast<float>(s + 1) / static_cast<float>(segments);
            const auto pointAt = [&](float u, float v) {
                const float phi = 2.0f * kPi * u;
                const float theta = kPi * v;
                return Vec3(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
            };
            const Vec3 d00 = pointAt(u0, v0), d10 = pointAt(u1, v0);
            const Vec3 d11 = pointAt(u1, v1), d01 = pointAt(u0, v1);
            const auto tangentAt = [](float u) {
                const float phi = 2.0f * kPi * u;
                return Vec3(-std::sin(phi), 0.0f, std::cos(phi));
            };
            const Vec2 uv00(u0 * uvScale, v0 * uvScale), uv10(u1 * uvScale, v0 * uvScale);
            const Vec2 uv11(u1 * uvScale, v1 * uvScale), uv01(u0 * uvScale, v1 * uvScale);
            const Vec3 t0 = tangentAt(u0), t1 = tangentAt(u1);
            part.triangles.push_back(ShadingTriangle{
                attribute(center + d00 * radius, d00, t0, uv00),
                attribute(center + d10 * radius, d10, t1, uv10),
                attribute(center + d11 * radius, d11, t1, uv11)});
            part.triangles.push_back(ShadingTriangle{
                attribute(center + d00 * radius, d00, t0, uv00),
                attribute(center + d11 * radius, d11, t1, uv11),
                attribute(center + d01 * radius, d01, t0, uv01)});
        }
    }
    return part;
}

// 地面：一整块平面（法向 +y，切线 +x）
Part makeFloor(float halfSize, float y, const Material& material, const Texture* map, float uvScale) {
    Part part;
    part.material = material;
    part.normalMap = map;
    addQuad(part, Vec3(-halfSize, y, -halfSize), Vec3(2.0f * halfSize, 0, 0), Vec3(0, 0, 2.0f * halfSize),
            Vec3(0, 1, 0), uvScale);
    return part;
}

Material makeMaterial(Vec3 diffuse, Vec3 specular, float shininess) {
    Material m;
    m.diffuse = diffuse;
    m.specular = specular;
    m.shininess = shininess;
    return m;
}

struct Options {
    std::string outputPath = "output/shaded-raster.png";
    int width = 800;
    int height = 600;
    float strength = 1.0f;
};

void printUsage() {
    std::cout <<
        "用法：\n"
        "  ShadedRaster [--output <out.png>] [--size WxH] [--strength s] [--help]\n"
        "\n"
        "  --output <file>  结果 PNG 路径（默认 output/shaded-raster.png，父目录自动创建）\n"
        "  --size WxH       画布尺寸（默认 800x600）\n"
        "  --strength s     凹凸强度（默认 1.0；0 = 贴图退化为平整法向）\n"
        "  --help           显示本帮助\n";
}

bool parseArgs(int argc, char** argv, Options& opt, int& exitCode) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { printUsage(); exitCode = 0; return false; }
        if (arg == "--output" && i + 1 < argc) { opt.outputPath = argv[++i]; }
        else if (arg == "--strength" && i + 1 < argc) { opt.strength = static_cast<float>(std::atof(argv[++i])); }
        else if (arg == "--size" && i + 1 < argc) {
            const std::string size = argv[++i];
            const std::size_t sep = size.find('x');
            if (sep == std::string::npos) {
                std::cerr << "错误：--size 需要 WxH 形式\n";
                exitCode = 2;
                return false;
            }
            opt.width = std::atoi(size.substr(0, sep).c_str());
            opt.height = std::atoi(size.substr(sep + 1).c_str());
        } else {
            std::cerr << "错误：无法识别的参数 " << arg << "（用 --help）\n";
            exitCode = 2;
            return false;
        }
    }
    if (opt.width < 32) opt.width = 32;
    if (opt.height < 32) opt.height = 32;
    if (opt.strength < 0.0f) opt.strength = 0.0f;
    return true;
}

} // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    SetConsoleOutputCP(65001);
#endif
    Options opt;
    int exitCode = 0;
    if (!parseArgs(argc, argv, opt, exitCode)) return exitCode;

    // ---- 程序化法线贴图（与光线跟踪那边同一套生成器）----
    const Texture brick = gfx::makeNormalMap(256, [](float u, float v) {
        return gfx::brickHeight(u, v, /*rows=*/5, /*columns=*/4, /*mortarWidth=*/0.05f, /*amplitude=*/0.24f);
    }, opt.strength);
    const Texture wave = gfx::makeNormalMap(256, [](float u, float v) {
        return gfx::waveHeight(u, v, /*frequency=*/6.0f, /*amplitude=*/0.22f);
    }, opt.strength);
    const Texture checker = gfx::makeNormalMap(256, [](float u, float v) {
        return gfx::checkerHeight(u, v, /*cells=*/6, /*amplitude=*/0.22f);
    }, opt.strength);

    // ---- 场景（光栅化路径：全部由三角形组成）----
    std::vector<Part> parts;
    parts.push_back(makeFloor(6.0f, -1.0f, makeMaterial(Vec3(0.34f, 0.36f, 0.40f), Vec3(0.35f, 0.35f, 0.35f), 64.0f),
                              &checker, 6.0f));
    parts.push_back(makeCube(Vec3(-1.5f, -0.35f, 0.0f), 0.65f,
                             makeMaterial(Vec3(0.70f, 0.42f, 0.22f), Vec3(0.55f, 0.55f, 0.55f), 64.0f),
                             &brick, 1.5f));
    parts.push_back(makeSphere(Vec3(1.6f, -0.35f, 0.1f), 0.7f, 28, 44,
                               makeMaterial(Vec3(0.30f, 0.52f, 0.78f), Vec3(0.80f, 0.80f, 0.80f), 128.0f),
                               &wave, 1.5f));

    std::vector<Light> lights;
    lights.push_back(Light{Vec3(3.5f, 4.5f, 4.0f), Vec3(1.00f, 0.96f, 0.90f), 1.10f});
    lights.push_back(Light{Vec3(-4.0f, 2.5f, 1.5f), Vec3(1.00f, 0.72f, 0.50f), 0.50f});

    // ---- 相机 ----
    const Vec3 eye(0.0f, 1.30f, 4.60f);
    View view;
    view.setViewport(opt.width, opt.height);
    view.lookAt(eye, Vec3(0.0f, -0.25f, -0.10f), Vec3(0.0f, 1.0f, 0.0f));
    view.setPerspective(42.0f * kPi / 180.0f, static_cast<float>(opt.width) / static_cast<float>(opt.height),
                        0.1f, 100.0f);

    Canvas canvas(opt.width, opt.height);
    canvas.clear(Color(24, 28, 38));
    DepthBuffer depth(opt.width, opt.height);

    std::cout << "光栅化着色管线演示（顶点属性 → 透视校正插值 → 逐像素着色）\n"
                 "  分辨率        : " << opt.width << "x" << opt.height << "\n"
              << "  凹凸强度      : " << opt.strength << "\n"
              << "  场景          : 地面(棋格) + 立方体(砖缝) + 球(波纹)，全部由三角形拼出\n"
              << "  片元着色      : 采样法线贴图 → TBN 扰动 → shadeLocal(Blinn-Phong) + gamma\n";

    // ---- 逐部件逐三角形：光栅化 + 深度测试 + 片元着色 ----
    long long fragments = 0;
    long long triangles = 0;
    for (const Part& part : parts) {
        const Texture* map = part.normalMap;
        const Material& material = part.material;
        for (const ShadingTriangle& tri : part.triangles) {
            ++triangles;
            gfx::fillTriangleShaded(canvas, view, tri, [&](const Fragment& f) -> Vec3 {
                ++fragments;
                Vec3 normal = f.normal;
                if (map != nullptr && gfx::math::dot(f.tangent, f.tangent) > 0.0f) {
                    // 用插值后的法向/切线重建正交基（插值会让它们不再严格正交）
                    TangentBasis basis;
                    if (gfx::math::normalize(normal, basis.normal)) {
                        basis.tangent = gfx::orthogonalize(f.tangent, basis.normal, Vec3(1.0f, 0.0f, 0.0f));
                        basis.bitangent = gfx::math::cross(basis.normal, basis.tangent);
                        normal = gfx::perturbNormal(basis, gfx::decodeNormal(map->sample(f.uv.x, f.uv.y)));
                    }
                } else {
                    gfx::math::normalize(normal, normal);
                }

                Vec3 viewDir;
                if (!gfx::math::normalize(eye - f.position, viewDir)) viewDir = Vec3(0.0f, 0.0f, 1.0f);

                // 光栅化路径没有场景求交 ⇒ 阴影测试恒为"不遮挡"
                const Vec3 linear = gfx::shadeLocal(
                    f.position, normal, viewDir, material, lights,
                    [](const Vec3&, const Vec3&, float) { return false; });

                // 输出前做伽马（shading.hpp 负责 0..1 → 8bit 量化，不做色彩空间变换）
                return Vec3(std::pow(std::max(0.0f, linear.x), 1.0f / 2.2f),
                            std::pow(std::max(0.0f, linear.y), 1.0f / 2.2f),
                            std::pow(std::max(0.0f, linear.z), 1.0f / 2.2f));
            }, &depth);
        }
    }

    std::string error;
    if (!gfx::savePng(canvas, opt.outputPath.c_str(), &error)) {
        std::cerr << "错误：保存 PNG 失败 —— " << error << "\n";
        return 4;
    }
    std::cout << "  三角形数      : " << triangles << "\n"
                 "  着色片元数    : " << fragments << "\n"
                 "  输出          : " << opt.outputPath << "\n";
    return 0;
}
