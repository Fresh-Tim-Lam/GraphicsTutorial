// euler_interactive.cpp —— 交互式演示：欧拉操作构造立方体并"掏空"（单步 + 可旋转）
//
// 用法：build\tutorials\EulerInteractive.exe
//   --dump         仅打印全部步骤（不开窗）
//   --audit        打印最终状态的所有面（外环 + 内环）
//   --audit-at N   打印第 N 步结束时的所有面
//   --shaded       以"面渲染"模式启动
//   --png <path>   导出"面渲染"与"线框"两张 PNG
//   --selfcheck    量化自检：正俯视/正仰视统计像素，证明上下方孔可见
//
// 操作（窗口获得焦点时）：
//   →  / 空格      前进一步            ←      后退一步
//   左键拖动       旋转视角（球坐标）   滚轮    缩放
//   R              重置视角            Esc/Q   退出
// 说明：窗口中**不显示文字**（本库暂无字体），每一步的说明与拓扑计数打印在控制台。
//
// 演示内容（28 步）：
//   ① MVFS + MEV×3 + MEF + MEV×4 + MEF×4   ⇒ **封闭立方体**（V=8 E=12 F=6，V−E+F=2）
//   ② 顶面：桥 + 内圈 + MEF + KEMR + KFMRH  ⇒ 顶面得到**两个内环**（同一圈顶点、方向相反）
//   ③ 底面：同样手法                        ⇒ 底面也有两个内环
//   ④ mefr(顶内环, 底内环)                   ⇒ 4 条竖棱 + 4 张内腔壁 ⇒ 上下贯通（亏格 1 的贯通孔）
//
// 权威实现：gfx::HalfEdgeMesh（halfedge.hpp）；渲染用 gfx::View（view.hpp）+ 新增的窗口输入（window.hpp）

#include <gfx/core/canvas.hpp>
#include <gfx/core/color.hpp>
#include <gfx/render/depth.hpp>
#include <gfx/geom/halfedge.hpp>
#include <gfx/render/png.hpp>
#include <gfx/render/triangle.hpp>
#include <gfx/render/view.hpp>
#include <gfx/render/window.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using gfx::Canvas;
using gfx::Color;
using gfx::HalfEdgeMesh;
using gfx::MeshId;
using gfx::View;
using gfx::rgb;
using gfx::math::Vec2;
using gfx::math::Vec3;

const Color kBackground = rgb(14, 16, 24);
const Color kEdgeColor = rgb(120, 200, 240);
const Color kInnerColor = rgb(250, 180, 80);     // 内环（孔）的边
const Color kVertexColor = rgb(255, 255, 255);
const Color kNewEdgeColor = rgb(255, 240, 120);  // 本步新增的边

// ---- 面渲染配色（按面 id 映射到固定调色板 ⇒ 同一面的颜色逐步、逐帧都稳定，不随机）----
// 6 个**外表面**：冷色系（蓝/青/灰，各面深浅不同），面 id 0..5 各取一色
const Color kOuterFaceColors[6] = {
    rgb( 64, 104, 196),   // 面0 顶面：蓝
    rgb( 56, 140, 180),   // 面1 底面：青蓝
    rgb( 88, 128, 168),   // 面2 y=0：灰蓝
    rgb( 72, 168, 196),   // 面3 x=2：青
    rgb(104, 120, 200),   // 面4 y=2：蓝紫
    rgb(120, 140, 156),   // 面5 x=0：灰
};
// 4 个**内腔壁**：高饱和暖色，四壁四色（面 id 8..11 ⇒ 8%4..11%4 = 0..3，互不相同）
const Color kInnerFaceColors[4] = {
    rgb(255, 122,   0),   // 橙 #FF7A00
    rgb(255, 212,   0),   // 黄 #FFD400
    rgb(255,  59,  48),   // 红 #FF3B30
    rgb(255,  45, 149),   // 品红 #FF2D95
};
const Color kShadedEdgeColor = rgb(16, 18, 24);  // 面渲染的描边色（深色，保证面边界清晰）

// 一步 = 一张拓扑快照 + 说明
struct Step {
    HalfEdgeMesh mesh;
    std::string label;
};

// 记录"当前网格里有哪些边"（按顶点对），用于高亮本步新增的边
struct EdgeSet {
    std::vector<std::pair<MeshId, MeshId>> keys;
    bool has(MeshId a, MeshId b) const {
        for (const auto& k : keys)
            if ((k.first == a && k.second == b) || (k.first == b && k.second == a)) return true;
        return false;
    }
};

EdgeSet collectEdges(const HalfEdgeMesh& mesh) {
    EdgeSet s;
    mesh.forEachEdge([&](MeshId, MeshId v0, MeshId v1) { s.keys.emplace_back(v0, v1); });
    return s;
}

// 构造 18 步：立方体（12 步）+ 掏空（6 步）
std::vector<Step> buildSteps() {
    std::vector<Step> steps;
    HalfEdgeMesh m;

    const auto push = [&](const std::string& label) { steps.push_back(Step{m, label}); };

    // ---- 立方体：底面 A(0,0,0) B(2,0,0) C(2,2,0) D(0,2,0)，顶面 E/F/G/H（z = 2）----
    m.mvfs(Vec3(0.0f, 0.0f, 0.0f));                   // v0 = A
    push("MVFS(A)：建体、面、外环、顶点");
    m.mevInLoop(0, 0, Vec3(2.0f, 0.0f, 0.0f));        // v1 = B
    push("MEV(A→B)：环上建边与点");
    m.mevInLoop(0, 1, Vec3(2.0f, 2.0f, 0.0f));        // v2 = C
    push("MEV(B→C)");
    m.mevInLoop(0, 2, Vec3(0.0f, 2.0f, 0.0f));        // v3 = D
    push("MEV(C→D)");
    m.mef(0, 3, 0);                                   // 底面（D→A 闭合）
    push("MEF(D→A)：闭合底面 ⇒ 边+1 面+1 环+1");

    m.mevInLoop(0, 0, Vec3(0.0f, 0.0f, 2.0f));        // v4 = E（A 上方）
    push("MEV(A→E)：向上拉出侧棱");
    m.mevInLoop(0, 4, Vec3(2.0f, 0.0f, 2.0f));        // v5 = F（E 旁）
    push("MEV(E→F)：建顶边");
    m.mef(0, 5, 1);                                   // 侧面 1（A,B,F,E）
    push("MEF(F→B)：切出侧面 1");

    m.mevInLoop(0, 5, Vec3(2.0f, 2.0f, 2.0f));        // v6 = G（在 F 处插！顺序才对）
    push("MEV(F→G)：在 F 处插入，保证环顺序与几何相邻一致");
    m.mef(0, 6, 2);                                   // 侧面 2（B,C,G,F）
    push("MEF(G→C)：切出侧面 2");

    m.mevInLoop(0, 6, Vec3(0.0f, 2.0f, 2.0f));        // v7 = H（G 旁）
    push("MEV(G→H)");
    m.mef(0, 7, 3);                                   // 侧面 3（C,D,H,G）
    push("MEF(H→D)：切出侧面 3");
    m.mef(0, 7, 4);                                   // 侧面 4（D,A,E,H）⇒ 顶面自动成为 f0
    push("MEF(H→E)：切出侧面 4 ⇒ 封闭立方体（V=8 E=12 F=6）");

    // ---- ② 顶面挖孔：桥 + 内圈 + MEF + KEMR + KFMRH ⇒ 顶面得到**两个内环** ----
    //   桥：从顶面外环某顶点拉一条"往返边"（两条半边同处一环）；内圈：在桥端点处依次插入 4 点；
    //   MEF 用 P3—P0 闭合内圈 ⇒ 切出一个"盖子"面；KEMR 删桥 ⇒ 顶面出现内环（孔）；
    //   KFMRH 把盖子并回顶面 ⇒ 顶面得到第二个内环（与第一个同圈顶点、方向相反）。
    const MeshId topLoop = m.face(0).outerLoop;
    const MeshId topAnchor = m.loopVertices(topLoop)[0];
    const MeshId tp0 = m.mevInLoop(topLoop, topAnchor, Vec3(0.5f, 0.5f, 2.0f));
    push("顶面 MEV(锚点→P0)：从顶面顶点拉出桥的一端（随后删桥）");
    const MeshId tp1 = m.mevInLoop(topLoop, tp0, Vec3(1.5f, 0.5f, 2.0f));
    push("顶面 MEV(P0→P1)：沿内圈走 1 步");
    const MeshId tp2 = m.mevInLoop(topLoop, tp1, Vec3(1.5f, 1.5f, 2.0f));
    push("顶面 MEV(P1→P2)：2 步");
    const MeshId tp3 = m.mevInLoop(topLoop, tp2, Vec3(0.5f, 1.5f, 2.0f));
    push("顶面 MEV(P2→P3)：3 步（内圈 4 点就绪）");
    const MeshId topCap = m.mef(topLoop, tp3, tp0);
    push("顶面 MEF(P3→P0)：闭合内圈 ⇒ 切出“盖子”面");
    m.kemr(m.edgeBetween(topAnchor, tp0));
    push("顶面 KEMR(桥)：删桥接边 ⇒ 顶面出现内环（孔 1）");
    m.kfmrh(topCap, 0);
    push("顶面 KFMRH(盖子 → 顶面)：盖子外环转为内环 ⇒ 顶面有**两个内环**（反向同圈）");

    // ---- ③ 底面同样处理 ⇒ 底面也有两个内环 ----
    const MeshId botLoop = m.face(1).outerLoop;
    const MeshId botAnchor = m.loopVertices(botLoop)[0];
    const MeshId bp0 = m.mevInLoop(botLoop, botAnchor, Vec3(0.5f, 0.5f, 0.0f));
    push("底面 MEV(锚点→Q0)：与顶面同样的桥手法");
    const MeshId bp1 = m.mevInLoop(botLoop, bp0, Vec3(1.5f, 0.5f, 0.0f));
    push("底面 MEV(Q0→Q1)：1 步");
    const MeshId bp2 = m.mevInLoop(botLoop, bp1, Vec3(1.5f, 1.5f, 0.0f));
    push("底面 MEV(Q1→Q2)：2 步");
    const MeshId bp3 = m.mevInLoop(botLoop, bp2, Vec3(0.5f, 1.5f, 0.0f));
    push("底面 MEV(Q2→Q3)：3 步");
    const MeshId botCap = m.mef(botLoop, bp3, bp0);
    push("底面 MEF(Q3→Q0)：闭合内圈 ⇒ 切出“盖子”面");
    m.kemr(m.edgeBetween(botAnchor, bp0));
    push("底面 KEMR(桥)：删桥 ⇒ 底面出现内环（孔 1）");
    m.kfmrh(botCap, 1);
    push("底面 KFMRH(盖子 → 底面)：底面也有**两个内环**");

    // ---- ④ mefr：把顶面一个内环与底面一个内环缝成 4 条竖棱 + 4 张内腔壁 ----
    std::vector<MeshId> topInners, botInners;
    for (MeshId l : m.face(0).loops) if (!m.loop(l).isOuter) topInners.push_back(l);
    for (MeshId l : m.face(1).loops) if (!m.loop(l).isOuter) botInners.push_back(l);
    // mefr 以"反向索引"复用两环自身的半边（几何不扭）；故在底面的两个内环中挑与顶内环反向存储者。
    const MeshId topRing = topInners[0];
    MeshId botRing = botInners[0];
    {
        const std::vector<MeshId> va = m.loopVertices(topRing);
        const std::vector<MeshId> vb = m.loopVertices(botRing);
        const auto d2 = [&](MeshId x, MeshId y) {
            const Vec3 a = m.vertex(x).position, b = m.vertex(y).position;
            const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            return dx * dx + dy * dy + dz * dz;
        };
        int off = 0;
        for (int k = 1; k < static_cast<int>(vb.size()); ++k)
            if (d2(va[0], vb[static_cast<std::size_t>(k)]) < d2(va[0], vb[static_cast<std::size_t>(off)])) off = k;
        const int nn = static_cast<int>(vb.size());
        if (d2(va[1], vb[static_cast<std::size_t>((off + 1) % nn)])
            < d2(va[1], vb[static_cast<std::size_t>((off - 1 + nn) % nn)]))
            botRing = botInners[1];
    }
    m.mefr(topRing, botRing);
    push("mefr(顶内环, 底内环)：一次建出 4 条竖棱 + 4 张内腔壁 ⇒ 上下贯通（亏格 1）");

    return steps;
}

// 相机：球坐标方位角/仰角 + **正交视体半高**
// @note 缩放要改的是**视体大小**：正交投影下改相机距离**不会**改变成像大小（无近大远小）
struct Camera {
    float azimuth = 0.9f;
    float elevation = 0.55f;
    float halfHeight = 2.6f;   // 滚轮改它 ⇒ 放大/缩小
    float distance = 14.0f;    // 仅用于摆相机位置
};

void renderMesh(Canvas& canvas, const View& view, const HalfEdgeMesh& mesh,
                const EdgeSet* previous) {
    mesh.forEachEdge([&](MeshId edgeId, MeshId v0, MeshId v1) {
        const MeshId h = mesh.edge(edgeId).halfEdge;
        const MeshId loop = mesh.halfEdge(h).loop;
        const bool inner = (loop != gfx::kNoMesh) && !mesh.loop(loop).isOuter;
        Color color = kEdgeColor;
        if (inner) color = kInnerColor;                              // 内环（孔）
        else if (previous && !previous->has(v0, v1)) color = kNewEdgeColor;   // 本步新增
        view.drawLine3D(canvas, mesh.vertex(v0).position, mesh.vertex(v1).position, color);
    });
    mesh.forEachVertex([&](MeshId v) {
        const Vec2 s = view.projectToPixel(mesh.vertex(v).position);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                canvas.putPixel(static_cast<int>(s.x) + dx, static_cast<int>(s.y) + dy, kVertexColor);
    });
}

// 由相机参数构造 View（正交视体随 halfHeight 缩放 ⇒ 滚轮缩放；正交下改相机距离无效）
View makeView(const Canvas& canvas, const Camera& cam) {
    const Vec3 target(1.0f, 1.0f, 1.0f);
    const Vec3 eye(target.x + cam.distance * std::cos(cam.elevation) * std::cos(cam.azimuth),
                    target.y + cam.distance * std::cos(cam.elevation) * std::sin(cam.azimuth),
                    target.z + cam.distance * std::sin(cam.elevation));
    View view;
    view.setViewport(canvas.width(), canvas.height());
    view.lookAt(eye, target, Vec3(0.0f, 0.0f, 1.0f));
    const float aspect = static_cast<float>(canvas.width()) / static_cast<float>(canvas.height());
    const float hh = cam.halfHeight;
    view.setOrthographic(-hh * aspect, hh * aspect, -hh, hh, 0.1f, 100.0f);
    return view;
}

// 偶数规则（射线穿越）判断点是否在多边形内 —— 专用于"内环（孔）不填充"
bool pointInPolygon(const std::vector<Vec2>& poly, const Vec2& p) {
    bool inside = false;
    const std::size_t n = poly.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const Vec2& a = poly[i];
        const Vec2& b = poly[j];
        if (((a.y > p.y) != (b.y > p.y)) &&
            (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x))
            inside = !inside;
    }
    return inside;
}

// 带深度测试的三角形填充；**跳过落在任一孔（内环）多边形内的像素**。
// @note 与 gfx::fillTriangleDepth 等价，只是多一层"孔洞遮罩"。为什么必须加：
//       外环是凸四边形时，其扇形三角化会把中间的孔一并盖住（孔就看不见了）；
//       这一层把孔真正挖空 ⇒ 孔处才能透出内腔壁。这仍满足"内环不填充"的约定。
void fillTriangleDepthKeepHoles(Canvas& canvas, gfx::DepthBuffer& depth,
                                const Vec2& a, const Vec2& b, const Vec2& c,
                                const Vec3& depth3, const Color& color,
                                const std::vector<std::vector<Vec2>>& holes) {
    if (gfx::edgeFunction(a, b, c) == 0.0f) return;

    const int xBegin = static_cast<int>(std::floor(std::min({a.x, b.x, c.x})));
    const int xEnd = static_cast<int>(std::ceil(std::max({a.x, b.x, c.x})));
    const int yBegin = static_cast<int>(std::floor(std::min({a.y, b.y, c.y})));
    const int yEnd = static_cast<int>(std::ceil(std::max({a.y, b.y, c.y})));

    for (int y = yBegin; y < yEnd; ++y) {
        for (int x = xBegin; x < xEnd; ++x) {
            const Vec2 p(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
            if (!gfx::insideTriangle(p, a, b, c)) continue;

            bool inHole = false;
            for (const auto& h : holes)
                if (h.size() >= 3 && pointInPolygon(h, p)) { inHole = true; break; }
            if (inHole) continue;   // 内环（孔）不填充

            Vec3 w;
            if (!gfx::barycentric(p, a, b, c, w)) continue;
            const float z = w.x * depth3.x + w.y * depth3.y + w.z * depth3.z;
            if (depth.testAndSet(x, y, z)) canvas.putPixel(x, y, color);
        }
    }
}

// 判断面是否"朝内"（内腔壁）：面法向 · (面中心 − 实体中心) < 0
bool isInnerFace(const HalfEdgeMesh& mesh, const std::vector<MeshId>& verts, const Vec3& bodyCenter) {
    const Vec3 pa = mesh.vertex(verts[0]).position;
    const Vec3 pb = mesh.vertex(verts[1]).position;
    const Vec3 pc = mesh.vertex(verts[2]).position;
    const Vec3 u(pb.x - pa.x, pb.y - pa.y, pb.z - pa.z);
    const Vec3 w(pc.x - pa.x, pc.y - pa.y, pc.z - pa.z);
    const Vec3 nrm(u.y * w.z - u.z * w.y, u.z * w.x - u.x * w.z, u.x * w.y - u.y * w.x);
    Vec3 fc(0.0f, 0.0f, 0.0f);
    for (MeshId vi : verts) {
        const Vec3 p = mesh.vertex(vi).position;
        fc.x += p.x; fc.y += p.y; fc.z += p.z;
    }
    const float inv = 1.0f / static_cast<float>(verts.size());
    return nrm.x * (fc.x * inv - bodyCenter.x)
         + nrm.y * (fc.y * inv - bodyCenter.y)
         + nrm.z * (fc.z * inv - bodyCenter.z) < 0.0f;
}

// 面 → 颜色：内腔壁用 4 色暖色、外表面用 6 色冷色，均按面 id 取模 ⇒ 颜色随面固定、不随机
Color faceColor(MeshId f, bool inner) {
    if (inner) return kInnerFaceColors[static_cast<std::size_t>(f) % 4u];
    return kOuterFaceColors[static_cast<std::size_t>(f) % 6u];
}

// 面渲染（在给定 View 下）：每个面取**外环** → 投影 → 扇形三角化 → 带深度测试填充；
//   **内环（孔）不填充**；最后用深色描边，保证面的边界清晰。
// @note 内环不填充 ⇒ 孔处透出里面的面（这正是验证"掏空"是否正确的关键）。
void renderShadedOnView(Canvas& canvas, const View& view, const HalfEdgeMesh& mesh) {
    canvas.clear(kBackground);
    gfx::DepthBuffer depth(canvas.width(), canvas.height());
    const Vec3 bodyCenter(1.0f, 1.0f, 1.0f);

    mesh.forEachFace([&](MeshId f) {
        const gfx::MeshFace& face = mesh.face(f);
        const std::vector<MeshId> verts = mesh.loopVertices(face.outerLoop);
        if (verts.size() < 3) return;

        std::vector<Vec2> px;
        std::vector<float> pz;
        px.reserve(verts.size());
        pz.reserve(verts.size());
        for (MeshId v : verts) {
            const Vec3 p = mesh.vertex(v).position;
            px.push_back(view.projectToPixel(p));
            gfx::math::Vec3 ndc;
            pz.push_back(view.projectToNdc(p, ndc) ? ndc.z : 1.0f);   // NDC 深度：越小越近
        }

        const bool inner = isInnerFace(mesh, verts, bodyCenter);
        const Color color = faceColor(f, inner);

        // 投影该面的所有内环（孔），填充时把它们挖空
        std::vector<std::vector<Vec2>> holes;
        for (MeshId l : face.loops) {
            if (l == face.outerLoop) continue;
            const std::vector<MeshId> iv = mesh.loopVertices(l);
            std::vector<Vec2> hp;
            hp.reserve(iv.size());
            for (MeshId v : iv) hp.push_back(view.projectToPixel(mesh.vertex(v).position));
            if (hp.size() >= 3) holes.push_back(std::move(hp));
        }

        for (std::size_t i = 1; i + 1 < verts.size(); ++i) {
            fillTriangleDepthKeepHoles(canvas, depth, px[0], px[i], px[i + 1],
                                       Vec3(pz[0], pz[i], pz[i + 1]), color, holes);
        }
    });

    // 描边：所有边用深色画出（线框描边保留），保证面的边界清晰
    mesh.forEachEdge([&](MeshId, MeshId v0, MeshId v1) {
        view.drawLine3D(canvas, mesh.vertex(v0).position, mesh.vertex(v1).position, kShadedEdgeColor);
    });
}

// 面渲染（用相机参数构造 View）
void renderShaded(Canvas& canvas, const std::vector<Step>& steps, int index, const Camera& cam) {
    renderShadedOnView(canvas, makeView(canvas, cam), steps[static_cast<std::size_t>(index)].mesh);
}

void renderFrame(Canvas& canvas, const std::vector<Step>& steps, int index, const Camera& cam) {
    canvas.clear(kBackground);
    const View view = makeView(canvas, cam);

    // 高亮"本步新增的边"：与本步之前的那张快照比对
    EdgeSet prevEdges;
    const EdgeSet* prevPtr = nullptr;
    if (index > 0) {
        prevEdges = collectEdges(steps[static_cast<std::size_t>(index - 1)].mesh);
        prevPtr = &prevEdges;
    }
    renderMesh(canvas, view, steps[static_cast<std::size_t>(index)].mesh, prevPtr);
}

void printStep(const std::vector<Step>& steps, int index) {
    const Step& s = steps[static_cast<std::size_t>(index)];
    const gfx::MeshCounts c = s.mesh.counts();
    std::printf("[%d/%d] %s\n        V=%d E=%d F=%d L=%d（内环 %d） V-E+F=%d  校验：%s\n",
                index + 1, static_cast<int>(steps.size()), s.label.c_str(),
                c.vertices, c.edges, c.faces, c.loops, c.innerLoops,
                s.mesh.eulerCharacteristic(), s.mesh.validate(nullptr) ? "通过" : "失败");
    std::fflush(stdout);
}

// 打印某个网格的**所有面**（含内环）—— 用于逐面核对拓扑（--audit / --audit-at）
void printAudit(const HalfEdgeMesh& m, int stepNo) {
    const gfx::MeshCounts c = m.counts();
    printf("=== 第 %d 步后：V=%d E=%d F=%d L=%d（内环 %d）V-E+F=%d 校验：%s ===\n",
           stepNo, c.vertices, c.edges, c.faces, c.loops, c.innerLoops,
           m.eulerCharacteristic(), m.validate(nullptr) ? "通过" : "失败");
    m.forEachFace([&](MeshId f) {
        const gfx::MeshFace& face = m.face(f);
        const std::vector<MeshId> vs = m.loopVertices(face.outerLoop);
        printf("  面%2d 环数=%zu 外环(%zu)：", f, face.loops.size(), vs.size());
        for (MeshId v : vs) {
            const Vec3 p = m.vertex(v).position;
            printf("v%d(%.1f,%.1f,%.1f) ", v, p.x, p.y, p.z);
        }
        printf("\n");
        for (MeshId l : face.loops) {
            if (l == face.outerLoop) continue;
            const std::vector<MeshId> iv = m.loopVertices(l);
            printf("          内环(%zu)：", iv.size());
            for (MeshId v : iv) {
                const Vec3 p = m.vertex(v).position;
                printf("v%d(%.1f,%.1f,%.1f) ", v, p.x, p.y, p.z);
            }
            printf("\n");
        }
    });
}

// ---- 量化自检：证明"孔可见" ----
// 正俯视（相机在正上方、垂直向下看顶面）与正仰视（正下方、垂直向上看底面）各渲染一帧，
// 统计"顶/底面颜色像素"与"暖色（4 个内腔壁）像素"。
//   暖色像素 > 0 ⇒ 透过顶面的方孔看到了内腔壁 ⇒ 孔是通的（否则孔被盖住/缺面）。
// @note 为什么用**透视**而不是正交：正交正俯视时，竖直的内腔壁投影退化为线段（面积 0），
//       一个暖色像素都不会有；透视下视线略带张角，才能看见孔壁（如同俯视一口井）。
//       这既贴合"从正上方垂直向下看"，也才能真实反映"孔是否透出内腔壁"。
void runSelfCheck(const std::vector<Step>& steps) {
    const HalfEdgeMesh& mesh = steps.back().mesh;
    const int W = 900, H = 700;

    // fromAbove = true：正俯视；false：正仰视。返回 (面颜色像素数, 暖色像素数)
    const auto count = [&](bool fromAbove) -> std::pair<int, int> {
        Canvas canvas(W, H);
        View view;
        view.setViewport(W, H);
        const Vec3 target(1.0f, 1.0f, 1.0f);
        const float dist = 6.0f;
        const Vec3 eye(1.0f, 1.0f, fromAbove ? 1.0f + dist : 1.0f - dist);
        view.lookAt(eye, target, Vec3(0.0f, 1.0f, 0.0f));   // 视线沿 ±z（垂直），上方向取 +y
        view.setPerspective(0.6f, static_cast<float>(W) / static_cast<float>(H), 0.1f, 100.0f);
        renderShadedOnView(canvas, view, mesh);

        const Color faceC = kOuterFaceColors[fromAbove ? 0u : 1u];   // 顶面 = 面0，底面 = 面1
        int facePix = 0, warmPix = 0;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const Color c = canvas.pixelAt(x, y);
                if (c == faceC) ++facePix;
                for (const Color& wc : kInnerFaceColors)
                    if (c == wc) { ++warmPix; break; }
            }
        }
        return std::make_pair(facePix, warmPix);
    };

    const std::pair<int, int> up = count(true);
    printf("[自检] 俯视：顶面色像素 = %d，暖色（内腔壁）像素 = %d  ⇒ 顶面方孔可见：%s\n",
           up.first, up.second, up.second > 0 ? "是" : "否");
    const std::pair<int, int> dn = count(false);
    printf("[自检] 仰视：底面色像素 = %d，暖色（内腔壁）像素 = %d  ⇒ 底面方孔可见：%s\n",
           dn.first, dn.second, dn.second > 0 ? "是" : "否");
    std::fflush(stdout);
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<Step> steps = buildSteps();

    bool shaded = false;
    const char* pngPath = nullptr;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--dump") {                                    // 只打印全部步骤（不开窗）
            for (int k = 0; k < static_cast<int>(steps.size()); ++k) printStep(steps, k);
            return 0;
        }
        if (a == "--audit-at" && i + 1 < argc) {                // 列出**指定步**结束时的所有面（用于对比中间态）
            const int stepNo = std::atoi(argv[++i]);
            if (stepNo < 1 || stepNo > static_cast<int>(steps.size())) {
                printf("步号超范围（1..%zu）\n", steps.size());
                return 2;
            }
            printAudit(steps[static_cast<std::size_t>(stepNo - 1)].mesh, stepNo);
            return 0;
        }
        if (a == "--audit") {                                   // 列出最终状态的所有面
            const HalfEdgeMesh& m = steps.back().mesh;
            const gfx::MeshCounts c = m.counts();
            printf("=== 最终状态：V=%d E=%d F=%d L=%d（内环 %d）V-E+F=%d 校验：%s ===\n",
                   c.vertices, c.edges, c.faces, c.loops, c.innerLoops,
                   m.eulerCharacteristic(), m.validate(nullptr) ? "通过" : "失败");
            printf("=== 面清单（每个面的外环顶点 + 内环）===\n");
            m.forEachFace([&](MeshId f) {
                const gfx::MeshFace& face = m.face(f);
                std::vector<MeshId> vs = m.loopVertices(face.outerLoop);
                printf("面%2d  环数=%zu  外环 %zu 顶点：", f, face.loops.size(), vs.size());
                for (MeshId v : vs) {
                    const Vec3 p = m.vertex(v).position;
                    printf("v%d(%.1f,%.1f,%.1f) ", v, p.x, p.y, p.z);
                }
                printf("\n");
                for (MeshId l : face.loops) {
                    if (l == face.outerLoop) continue;
                    const std::vector<MeshId> iv = m.loopVertices(l);
                    printf("        内环 %zu 顶点：", iv.size());
                    for (MeshId v : iv) {
                        const Vec3 p = m.vertex(v).position;
                        printf("v%d(%.1f,%.1f,%.1f) ", v, p.x, p.y, p.z);
                    }
                    printf("\n");
                }
            });
            return 0;
        }
        if (a == "--shaded") shaded = true;                     // 以"面渲染"启动
        if (a == "--selfcheck") {                               // 量化自检：证明孔可见（不开窗）
            runSelfCheck(steps);
            return 0;
        }
        if (a == "--png" && i + 1 < argc) pngPath = argv[++i];  // 渲染最后一步到 PNG
    }

    const int width = 900;
    const int height = 700;
    Camera cam;

    // --png：把**最后一步**（掏空完成）导出两张图：面渲染 + 线框，便于逐面核对拓扑
    if (pngPath != nullptr) {
        const int last = static_cast<int>(steps.size()) - 1;
        Canvas face(width, height);
        Canvas wire(width, height);
        renderShaded(face, steps, last, cam);
        renderFrame(wire, steps, last, cam);
        const std::string base(pngPath);
        const std::string wirePath = base + "-wire.png";
        std::string err;
        if (!gfx::savePng(face, base.c_str(), &err)) { printf("保存失败：%s\n", err.c_str()); return 5; }
        if (!gfx::savePng(wire, wirePath.c_str(), &err)) { printf("保存失败：%s\n", err.c_str()); return 5; }
        printf("已保存两张图：\n  %s（面渲染：能看到孔与内腔）\n  %s（线框）\n",
               base.c_str(), wirePath.c_str());
        return 0;
    }

    printf("================ 欧拉操作交互演示 ================\n");
    printf("→ / 空格：下一步    ←：上一步    左键拖动：旋转    滚轮：缩放    M：线框/面渲染    R：复位    Esc/Q：退出\n");
    printf("（窗口内不显示文字，步骤说明与拓扑计数打印在这里；线框：黄色 = 本步新增的边，橙色 = 内环/孔；"
           "面渲染：冷色 = 外表面，暖色 = 内腔壁）\n\n");

    gfx::Window window("欧拉操作：构造立方体并掏空（→ 单步 / 拖动旋转 / 滚轮缩放 / M 切面）",
                       width, height);
    if (!window.isOpen()) {
        printf("错误：窗口创建失败。\n");
        return 5;
    }

    int index = 0;
    printStep(steps, index);

    while (window.pollEvents()) {
        if (window.keyPressed('M')) {
            shaded = !shaded;
            printf("（渲染模式：%s）\n", shaded ? "面渲染（孔处会透出内腔）" : "线框");
        }
        const gfx::MouseState& ms = window.mouse();

        // 左键拖动 ⇒ 旋转
        if (ms.leftDown && (ms.dx != 0 || ms.dy != 0)) {
            cam.azimuth -= static_cast<float>(ms.dx) * 0.01f;
            cam.elevation += static_cast<float>(ms.dy) * 0.01f;
            const float lim = 1.5f;
            if (cam.elevation > lim) cam.elevation = lim;
            if (cam.elevation < -lim) cam.elevation = -lim;
        }
        // 滚轮 ⇒ 缩放（改**正交视体半高**；正交投影下改相机距离不会改变成像大小）
        if (ms.wheel != 0) {
            cam.halfHeight -= static_cast<float>(ms.wheel) * 0.25f;
            if (cam.halfHeight < 1.2f) cam.halfHeight = 1.2f;
            if (cam.halfHeight > 8.0f) cam.halfHeight = 8.0f;
        }
        // 单步（keyPressed 忽略系统自动重复 ⇒ 按一次走一步）
        if (window.keyPressed(gfx::Key::Right) || window.keyPressed(gfx::Key::Space)) {
            if (index + 1 < static_cast<int>(steps.size())) {
                ++index;
                printStep(steps, index);
            } else {
                printf("（已是最后一步）\n");
            }
        }
        if (window.keyPressed(gfx::Key::Left)) {
            if (index > 0) {
                --index;
                printStep(steps, index);
            }
        }
        if (window.keyPressed('R')) {
            cam = Camera{};
            printf("（视角已复位）\n");
        }
        if (window.keyPressed(gfx::Key::Escape) || window.keyPressed('Q')) {
            window.requestClose();
            break;
        }

        if (shaded) renderShaded(window.canvas(), steps, index, cam);
        else renderFrame(window.canvas(), steps, index, cam);
        window.present();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    printf("\n退出。\n");
    return 0;
}
