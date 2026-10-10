// euler_demo.cpp —— 教学示例：用欧拉操作构造/修改实体拓扑（作业二演示）
//
// 用途：演示五个欧拉操作的实际效果，并把结果**画出来**：
//       ① 连续调用 MVFS + MEV×3 + MEF×3 构造**四面体**（封闭体：V−E+F = 2）；
//       ② 构造 KEMR 前提（桥 + 内圈），执行 KEMR ⇒ 拆出**内环**（挖孔的拓扑基础）；
//       ③ 构造两个同体面，执行 KFMRH ⇒ 删面并把其外环转为另一面的**内环**。
//       每一步都打印拓扑信息并调用 validate() 检查结构一致性（环闭合、配对、前驱后继）。
// 权威实现：gfx::HalfEdgeMesh（src/include/gfx/geom/halfedge.hpp）
// 构建：cmake --build build --target tutorial_euler
// 运行：build/tutorials/EulerOps --output tutorials/euler-ops/output/euler-ops.png
//
// 参数：--output <png>（默认 output/euler-ops.png，目录需存在）/ --window / --size WxH / --help

#include <gfx/core/canvas.hpp>
#include <gfx/core/color.hpp>
#include <gfx/geom/halfedge.hpp>
#include <gfx/render/png.hpp>
#include <gfx/render/view.hpp>
#include <gfx/render/window.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

using gfx::Canvas;
using gfx::Color;
using gfx::HalfEdgeMesh;
using gfx::MeshCounts;
using gfx::MeshId;
using gfx::View;
using gfx::rgb;
using gfx::math::Vec3;

const Color kBackground = rgb(14, 16, 24);
const Color kEdgeColor = rgb(120, 200, 240);
const Color kInnerColor = rgb(250, 180, 80);   // 内环的边
const Color kVertexColor = rgb(255, 255, 255);

struct Options {
    bool useWindow = false;
    std::string outputPath = "output/euler-ops.png";
    int width = 1080;
    int height = 760;      // 3 列 × 2 行，每块 360×380
};

void printUsage() {
    std::cout <<
        "用法：\n"
        "  EulerOps [--output <out.png>] [--window] [--size WxH]\n"
        "  EulerOps --help\n"
        "\n"
        "  --output <file>   结果 PNG 路径（默认 output/euler-ops.png；目录需已存在）\n"
        "  --window          打开窗口展示\n"
        "  --size WxH        画布尺寸（默认 1000x420，三块：四面体 / KEMR / KFMRH）\n"
        "  --help            显示本帮助\n";
}

bool parseArgs(int argc, char** argv, Options& opt, int& exitCode) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { printUsage(); exitCode = 0; return false; }
        if (arg == "--window") { opt.useWindow = true; }
        else if (arg == "--output" && i + 1 < argc) { opt.outputPath = argv[++i]; }
        else if (arg == "--size" && i + 1 < argc) {
            const std::string size = argv[++i];
            const std::size_t sep = size.find('x');
            if (sep == std::string::npos) {
                std::cerr << "错误：--size 需要 WxH 形式，例如 1000x420\n";
                exitCode = 2;
                return false;
            }
            opt.width = std::atoi(size.substr(0, sep).c_str());
            opt.height = std::atoi(size.substr(sep + 1).c_str());
            if (opt.width < 300 || opt.height < 200) {
                std::cerr << "错误：画布尺寸过小（至少 300x200）\n";
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

// 逐步执行 + 打印：每步都查结构一致性（这是作业要求的"检查环闭合、归属、悬挂引用"）
void report(const HalfEdgeMesh& mesh, const std::string& step) {
    std::string error;
    const bool ok = mesh.validate(&error);
    const MeshCounts c = mesh.counts();
    std::cout << "  " << step << "  →  V=" << c.vertices << " E=" << c.edges << " F=" << c.faces
              << " L=" << c.loops << "（内环 " << c.innerLoops << "） S=" << c.solids
              << "  半边=" << c.halfEdges << "  V-E+F=" << mesh.eulerCharacteristic()
              << "  校验：" << (ok ? "通过" : ("失败 → " + error)) << "\n";
}

// ① 四面体：MVFS + MEV×3 + MEF×3（连续调用，最终为封闭体）
void buildTetrahedron(HalfEdgeMesh& mesh) {
    std::cout << "① 四面体：连续调用 MVFS → MEV×3 → MEF×3\n";
    mesh.mvfs(Vec3(0.0f, 0.0f, 0.0f));                     // v0 = A
    report(mesh, "MVFS(A)");
    mesh.mevInLoop(0, 0, Vec3(2.2f, 0.0f, 0.0f));          // v1 = B
    report(mesh, "MEV(A→B)");
    mesh.mevInLoop(0, 1, Vec3(1.1f, 2.0f, 0.0f));          // v2 = C
    report(mesh, "MEV(B→C)");
    mesh.mef(0, 2, 0);                                     // 闭合底面 ⇒ 新面
    report(mesh, "MEF(C→A)");
    mesh.mevInLoop(0, 0, Vec3(1.1f, 0.8f, 2.2f));          // v3 = D（从 A 拉出）
    report(mesh, "MEV(A→D)");
    mesh.mef(0, 3, 1);                                     // 侧面
    report(mesh, "MEF(D→B)");
    mesh.mef(0, 3, 2);                                     // 侧面
    report(mesh, "MEF(D→C)   ⇒ 四面体（V=4 E=6 F=4）");
}

// ② KEMR：桥 + 内圈 → 删桥 ⇒ 内环
void buildKemr(HalfEdgeMesh& mesh) {
    std::cout << "② KEMR：构造桥接边（桥 + 内圈），删桥 ⇒ 拆出内环\n";
    mesh.mvfs(Vec3(0.0f, 0.0f, 0.0f));                     // v0 = A
    mesh.mevInLoop(0, 0, Vec3(2.4f, 0.0f, 0.0f));          // v1 = B
    mesh.mevInLoop(0, 1, Vec3(2.4f, 2.4f, 0.0f));          // v2 = C
    mesh.mef(0, 2, 0);                                     // 三角形面；环 0 成为另一个三角形
    report(mesh, "MVFS+MEV×2+MEF（三角形面）");
    mesh.mevInLoop(0, 0, Vec3(0.9f, 1.1f, 0.35f));         // v3 = D（桥 A-D）
    mesh.mevInLoop(0, 3, Vec3(1.9f, 1.4f, 0.35f));         // v4 = E
    mesh.mevInLoop(0, 4, Vec3(1.4f, 2.2f, 0.35f));         // v5 = F
    mesh.mef(0, 5, 3);                                     // 闭合内圈
    report(mesh, "MEV×3 + MEF（桥 + 内圈）");

    const MeshId bridge = mesh.edgeBetween(0, 3);
    const MeshId h1 = mesh.edge(bridge).halfEdge;
    const bool sameLoop = mesh.halfEdge(h1).loop == mesh.halfEdge(mesh.halfEdge(h1).opposite).loop;
    std::cout << "    桥接边 A-D（边 " << bridge << "）的两条半边同环？" << (sameLoop ? "是（满足前提）" : "否") << "\n";
    mesh.kemr(bridge);
    report(mesh, "KEMR(A-D)  ⇒ 边 −1、环 +1、内环出现");
}

// ③ KFMRH：两个同体面 → 删一个面，其外环转为另一面的内环
void buildKfmrh(HalfEdgeMesh& mesh) {
    std::cout << "③ KFMRH：同体两个面，删 donor 面 ⇒ 其外环成为 receiver 的内环\n";
    mesh.mvfs(Vec3(0.0f, 0.0f, 0.0f));
    mesh.mevInLoop(0, 0, Vec3(2.4f, 0.0f, 0.0f));
    mesh.mevInLoop(0, 1, Vec3(2.4f, 2.4f, 0.0f));
    mesh.mef(0, 2, 0);                                     // f0 与 f1（同体、共享三条边）
    report(mesh, "MVFS+MEV×2+MEF（两个面）");
    mesh.kfmrh(1, 0);                                      // 删 f1，其外环并入 f0 作内环
    report(mesh, "KFMRH(f1 → f0)  ⇒ 面 −1、环总数不变、内环 +1");
}

// 构造演示序列：**六个关键状态** —— 对应五个欧拉操作对拓扑的逐步影响
std::vector<HalfEdgeMesh> buildSnapshots() {
    std::vector<HalfEdgeMesh> snaps;

    // ① MVFS + MEV×3：建体/面/环/点，再连续建边（环是"折线往返"）
    {
        HalfEdgeMesh m;
        m.mvfs(Vec3(0.0f, 0.0f, 0.0f));
        m.mevInLoop(0, 0, Vec3(2.2f, 0.0f, 0.0f));
        m.mevInLoop(0, 1, Vec3(1.1f, 2.0f, 0.0f));
        snaps.push_back(m);
    }
    // ② MEF：闭合 ⇒ 边 +1、面 +1、环 +1（一个环被拆成两个）
    {
        HalfEdgeMesh m = snaps.back();
        m.mef(0, 2, 0);
        snaps.push_back(m);
    }
    // ③ 再 MEV + MEF×2：拉出顶点 D ⇒ 封闭**四面体**（V=4 E=6 F=4）
    {
        HalfEdgeMesh m = snaps.back();
        m.mevInLoop(0, 0, Vec3(1.1f, 0.8f, 2.2f));
        m.mef(0, 3, 1);
        m.mef(0, 3, 2);
        snaps.push_back(m);
    }
    // ④ KEMR 前提：三角形面 + **桥接边** + 内圈（桥的两条半边同属一个环）
    {
        HalfEdgeMesh m;
        m.mvfs(Vec3(0.0f, 0.0f, 0.0f));
        m.mevInLoop(0, 0, Vec3(2.4f, 0.0f, 0.0f));
        m.mevInLoop(0, 1, Vec3(2.4f, 2.4f, 0.0f));
        m.mef(0, 2, 0);
        m.mevInLoop(0, 0, Vec3(0.9f, 1.1f, 0.35f));      // 桥 A-D
        m.mevInLoop(0, 3, Vec3(1.9f, 1.4f, 0.35f));
        m.mevInLoop(0, 4, Vec3(1.4f, 2.2f, 0.35f));
        m.mef(0, 5, 3);                                  // 闭合内圈
        snaps.push_back(m);
    }
    // ⑤ KEMR：删除桥接边 ⇒ 边 −1、环 +1，新环是**内环**（图中橙色）
    {
        HalfEdgeMesh m = snaps.back();
        m.kemr(m.edgeBetween(0, 3));
        snaps.push_back(m);
    }
    // ⑥ KFMRH：删除一个面，把它的外环转为另一面的**内环**（图中橙色）
    {
        HalfEdgeMesh m;
        m.mvfs(Vec3(0.0f, 0.0f, 0.0f));
        m.mevInLoop(0, 0, Vec3(2.4f, 0.0f, 0.0f));
        m.mevInLoop(0, 1, Vec3(2.4f, 2.4f, 0.0f));
        m.mef(0, 2, 0);
        m.kfmrh(1, 0);
        snaps.push_back(m);
    }
    return snaps;
}

// 渲染：把网格的边画成线框（3D 正交投影；内环的边用另一种颜色）
void renderMesh(Canvas& canvas, const View& view, const HalfEdgeMesh& mesh) {
    // 先统计每条边属于"外环"还是"内环"
    mesh.forEachEdge([&](MeshId edgeId, MeshId v0, MeshId v1) {
        const MeshId h = mesh.edge(edgeId).halfEdge;
        const MeshId loop = mesh.halfEdge(h).loop;
        const bool inner = (loop != gfx::kNoMesh) && !mesh.loop(loop).isOuter;
        view.drawLine3D(canvas, mesh.vertex(v0).position, mesh.vertex(v1).position,
                        inner ? kInnerColor : kEdgeColor);
    });
    mesh.forEachVertex([&](MeshId v) {
        const Vec3 p = mesh.vertex(v).position;
        const gfx::math::Vec2 s = view.projectToPixel(p);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                canvas.putPixel(static_cast<int>(s.x) + dx, static_cast<int>(s.y) + dy, kVertexColor);
    });
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    int exitCode = 0;
    if (!parseArgs(argc, argv, opt, exitCode)) return exitCode;

    HalfEdgeMesh tetra, kemrMesh, kfmrhMesh;
    buildTetrahedron(tetra);
    buildKemr(kemrMesh);
    buildKfmrh(kfmrhMesh);

    // 画面：**六块快照**（3 列 × 2 行）—— 对应五个欧拉操作的逐步影响
    const std::vector<HalfEdgeMesh> snaps = buildSnapshots();
    const int cols = 3;
    const int rows = 2;
    const int panelW = opt.width / cols;
    const int panelH = opt.height / rows;

    Canvas canvas(opt.width, opt.height);
    canvas.clear(kBackground);
    for (std::size_t i = 0; i < snaps.size(); ++i) {
        View view;
        view.setViewport(panelW, panelH)
            .setViewportOrigin(static_cast<int>(i % cols) * panelW, static_cast<int>(i / cols) * panelH);
        view.lookAt(Vec3(3.2f, 2.6f, 6.0f), Vec3(1.2f, 1.2f, 0.4f), Vec3(0.0f, 1.0f, 0.0f));
        view.setOrthographic(-2.6f, 2.6f, -1.9f, 1.9f, 0.1f, 100.0f);
        renderMesh(canvas, view, snaps[i]);
    }

    if (opt.useWindow) {
        gfx::Window window("欧拉操作可视化：MVFS/MEV/MEF 构造四面体 + KEMR/KFMRH 生成内环（六块快照）",
                           opt.width, opt.height);
        if (!window.isOpen()) {
            std::cerr << "错误：窗口创建失败。\n";
            return 5;
        }
        window.canvas() = canvas;
        while (window.pollEvents()) {
            window.present();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

    std::string error;
    if (!gfx::savePng(canvas, opt.outputPath.c_str(), &error)) {
        std::cerr << "错误：保存 PNG 失败 —— " << error << "\n";
        return 5;
    }
    std::cout << "结果图（六块：MVFS/MEV/MEF 构造四面体 + KEMR / KFMRH 生成内环；内环为橙色）："
              << opt.outputPath << "\n";
    return 0;
}
