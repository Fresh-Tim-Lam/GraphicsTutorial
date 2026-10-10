// test_halfedge.cpp —— 半边数据结构与五个欧拉操作（MVFS / MEV / MEF / KEMR / KFMRH）
//
// 运行：ctest --test-dir build -C Release -R test_halfedge
//
// 测试策略：
//   1. 每个操作一组**满足其前提**的用例，写明操作前后的 V/E/F/L/S 数量与连接关系；
//   2. 数量变化只是辅助检查 —— 重点是 validate()：半边配对互反、next/prev 互反、
//      环闭合且顶点连续、面/环/体归属一致、无悬挂引用（删除对象不再被引用）；
//   3. 连续调用：MVFS + MEV×3 + MEF×3 构造**四面体**（封闭体，欧拉特征数 V−E+F = 2）；
//   4. 失败路径：参数非法或前提不满足时必须拒绝，且不破坏已有结构。

#include "gfx_test.hpp"

#include <gfx/geom/halfedge.hpp>

#include <cstdio>
#include <string>
#include <vector>

using gfx::HalfEdgeMesh;
using gfx::kNoMesh;
using gfx::math::Vec3;

namespace {

// 结构一致性 + 出错时打印原因（便于定位）
void checkValid(const HalfEdgeMesh& mesh, const char* tag) {
    std::string error;
    if (!mesh.validate(&error)) {
        std::printf("    [%s] validate 失败：%s\n", tag, error.c_str());
    }
    TEST_CHECK(mesh.validate(nullptr));
}

} // namespace

int main() {
    // ---------- 1. MVFS：建体、面、外环、顶点（无边）----------
    {
        HalfEdgeMesh mesh;
        const int solid = mesh.mvfs(Vec3(0.0f, 0.0f, 0.0f));
        TEST_CHECK(solid == 0);
        const gfx::MeshCounts c = mesh.counts();
        TEST_CHECK(c.solids == 1 && c.faces == 1 && c.loops == 1 && c.vertices == 1);
        TEST_CHECK(c.edges == 0 && c.halfEdges == 0);
        TEST_CHECK(mesh.eulerCharacteristic() == 2);          // 1 − 0 + 1
        // 归属关系：体的面集合 ← 面 → 外环；环此时还没有边
        TEST_CHECK(mesh.solid(0).faces.size() == 1 && mesh.solid(0).faces[0] == 0);
        TEST_CHECK(mesh.face(0).outerLoop == 0);
        TEST_CHECK(mesh.loop(0).face == 0 && mesh.loop(0).halfEdge == kNoMesh);
        TEST_CHECK(mesh.vertex(0).halfEdge == kNoMesh);       // 孤立点
        checkValid(mesh, "MVFS");
    }

    // ---------- 2. MEV 情形①：MVFS 之后环上还没有边 ----------
    {
        HalfEdgeMesh mesh;
        mesh.mvfs(Vec3(0.0f, 0.0f, 0.0f));
        const int v1 = mesh.mev(0, Vec3(1.0f, 0.0f, 0.0f));
        TEST_CHECK(v1 == 1);
        const gfx::MeshCounts c = mesh.counts();
        TEST_CHECK(c.vertices == 2 && c.edges == 1 && c.halfEdges == 2);
        TEST_CHECK(c.loops == 1 && c.faces == 1 && c.solids == 1);   // 面数环数不变

        // 环闭合：两条半边互为 next/prev，且首尾相接
        const int h = mesh.loop(0).halfEdge;
        TEST_CHECK(mesh.halfEdge(h).next == mesh.halfEdge(h).prev);
        TEST_CHECK(mesh.halfEdge(mesh.halfEdge(h).next).next == h);
        TEST_CHECK(mesh.halfEdge(h).end == mesh.halfEdge(mesh.halfEdge(h).next).start);
        // 配对互反、方向相反
        const int o = mesh.halfEdge(h).opposite;
        TEST_CHECK(mesh.halfEdge(o).opposite == h);
        TEST_CHECK(mesh.halfEdge(o).start == mesh.halfEdge(h).end);
        checkValid(mesh, "MEV-初始环");
    }

    // ---------- 3. MEV 情形②：环上已有边（在指定顶点处插入）----------
    {
        HalfEdgeMesh mesh;
        mesh.mvfs(Vec3(0.0f, 0.0f, 0.0f));
        mesh.mev(0, Vec3(1.0f, 0.0f, 0.0f));
        const int v2 = mesh.mev(1, Vec3(1.0f, 1.0f, 0.0f));
        TEST_CHECK(v2 == 2);
        const gfx::MeshCounts c = mesh.counts();
        TEST_CHECK(c.vertices == 3 && c.edges == 2 && c.halfEdges == 4);
        TEST_CHECK(c.loops == 1 && c.faces == 1);
        TEST_CHECK(mesh.edgeBetween(1, 2) != kNoMesh);        // 新点确实连到了 v1
        TEST_CHECK(mesh.edgeBetween(0, 2) == kNoMesh);        // 但没有连到 v0
        checkValid(mesh, "MEV-已有边");
    }

    // ---------- 4. MEF：一个环拆成两个环 + 新建一个面 ----------
    {
        HalfEdgeMesh mesh;
        mesh.mvfs(Vec3(0.0f, 0.0f, 0.0f));
        mesh.mev(0, Vec3(1.0f, 0.0f, 0.0f));                 // v1
        mesh.mev(1, Vec3(1.0f, 1.0f, 0.0f));                 // v2
        const int f1 = mesh.mef(0, 2, 0);                    // 环 0 中连 v2 → v0
        TEST_CHECK(f1 == 1);
        const gfx::MeshCounts c = mesh.counts();
        TEST_CHECK(c.vertices == 3 && c.edges == 3 && c.faces == 2 && c.loops == 2);
        TEST_CHECK(c.halfEdges == 6);
        TEST_CHECK(mesh.eulerCharacteristic() == 2);          // 3 − 3 + 2
        // 两个环分属两个面，且都闭合
        TEST_CHECK(mesh.loop(0).face != mesh.loop(1).face);
        TEST_CHECK(mesh.face(0).loops.size() == 1 && mesh.face(1).loops.size() == 1);
        checkValid(mesh, "MEF");
    }

    // ---------- 5. 连续调用：MVFS + MEV×3 + MEF×3 构造四面体（封闭体）----------
    {
        HalfEdgeMesh mesh;
        mesh.mvfs(Vec3(0.0f, 0.0f, 0.0f));                    // v0 = A
        mesh.mevInLoop(0, 0, Vec3(1.0f, 0.0f, 0.0f));         // v1 = B
        mesh.mevInLoop(0, 1, Vec3(1.0f, 1.0f, 0.0f));         // v2 = C
        TEST_CHECK(mesh.mef(0, 2, 0) != kNoMesh);             // 面 f1 = C-B-A
        mesh.mevInLoop(0, 0, Vec3(0.0f, 0.0f, 1.0f));         // v3 = D
        TEST_CHECK(mesh.mef(0, 3, 1) != kNoMesh);             // 面 f2 = D-A-B
        TEST_CHECK(mesh.mef(0, 3, 2) != kNoMesh);             // 面 f3 = D-B-C

        const gfx::MeshCounts c = mesh.counts();
        TEST_CHECK(c.vertices == 4 && c.edges == 6 && c.faces == 4 && c.loops == 4);
        TEST_CHECK(c.halfEdges == 12 && c.solids == 1);
        TEST_CHECK(mesh.eulerCharacteristic() == 2);           // 4 − 6 + 4（封闭体）
        checkValid(mesh, "四面体");

        // 四面体每个顶点度数 = 3（连接关系校验，不只查数量）
        for (int v = 0; v < 4; ++v) {
            int degree = 0;
            for (int e = 0; e < c.edges; ++e) {
                const gfx::MeshHalfEdge h = mesh.halfEdge(mesh.edge(e).halfEdge);
                if (h.start == v || h.end == v) ++degree;
            }
            TEST_CHECK(degree == 3);
        }
        // 六条边恰好是四面体的六条（每对顶点至多一条边）
        int pairCount = 0;
        for (int i = 0; i < 6; ++i)
            for (int j = i + 1; j < 6; ++j) {
                int shared = 0;
                const auto a = mesh.halfEdge(mesh.edge(i).halfEdge);
                const auto b = mesh.halfEdge(mesh.edge(j).halfEdge);
                if (a.start == b.start || a.start == b.end) ++shared;
                if (a.end == b.start || a.end == b.end) ++shared;
                if (shared == 2) ++pairCount;
            }
        TEST_CHECK(pairCount == 0);                            // 没有重复边
    }

    // ---------- 6. KEMR：删除桥接边 ⇒ 拆出内环 ----------
    {
        // 构造前提：先做三角形面，再用 MEV 拉出"桥 + 内圈"，MEF 闭合内圈 ⇒
        // 桥的两条半边留在同一个环里（= 桥接边），满足 KEMR 前提。
        HalfEdgeMesh mesh;
        mesh.mvfs(Vec3(0.0f, 0.0f, 0.0f));                    // v0 = A
        mesh.mevInLoop(0, 0, Vec3(1.0f, 0.0f, 0.0f));         // v1 = B
        mesh.mevInLoop(0, 1, Vec3(1.0f, 1.0f, 0.0f));         // v2 = C
        mesh.mef(0, 2, 0);                                    // 面 f1；环 0 变成另一个三角形
        mesh.mevInLoop(0, 0, Vec3(2.0f, 0.0f, 0.0f));         // v3 = D（桥 A-D）
        mesh.mevInLoop(0, 3, Vec3(3.0f, 0.0f, 0.0f));         // v4 = E
        mesh.mevInLoop(0, 4, Vec3(3.0f, 1.0f, 0.0f));         // v5 = F
        TEST_CHECK(mesh.mef(0, 5, 3) != kNoMesh);             // 闭合内圈
        checkValid(mesh, "KEMR-前提");

        const int bridge = mesh.edgeBetween(0, 3);
        TEST_CHECK(bridge != kNoMesh);
        // 前提：两条半边属于同一个环
        const int h1 = mesh.edge(bridge).halfEdge;
        const int h2 = mesh.halfEdge(h1).opposite;
        TEST_CHECK(mesh.halfEdge(h1).loop == mesh.halfEdge(h2).loop);

        const gfx::MeshCounts before = mesh.counts();
        TEST_CHECK(mesh.kemr(bridge));
        const gfx::MeshCounts after = mesh.counts();
        TEST_CHECK(after.edges == before.edges - 1);           // 边 −1
        TEST_CHECK(after.loops == before.loops + 1);           // 环 +1
        TEST_CHECK(after.innerLoops == 1);                     // 新的那个是内环
        TEST_CHECK(after.vertices == before.vertices && after.faces == before.faces);
        TEST_CHECK(!mesh.edge(bridge).alive);                  // 删除对象不再存活
        // 内环归属正确且闭合
        for (int i = 0; i < after.loops; ++i) {
            if (!mesh.loop(i).alive) continue;
            if (!mesh.loop(i).isOuter) TEST_CHECK(mesh.loop(i).face != kNoMesh);
        }
        // 原面同时拥有外环与内环
        TEST_CHECK(mesh.face(mesh.loop(0).face).loops.size() == 2);
        checkValid(mesh, "KEMR");
    }

    // ---------- 7. KFMRH：删除面，把它的外环转为另一面的内环 ----------
    {
        HalfEdgeMesh mesh;
        mesh.mvfs(Vec3(0.0f, 0.0f, 0.0f));
        mesh.mevInLoop(0, 0, Vec3(1.0f, 0.0f, 0.0f));
        mesh.mevInLoop(0, 1, Vec3(1.0f, 1.0f, 0.0f));
        mesh.mef(0, 2, 0);                                    // 同体的两个面（f0、f1），共享三条边
        const gfx::MeshCounts before = mesh.counts();
        TEST_CHECK(before.faces == 2 && before.innerLoops == 0);

        TEST_CHECK(mesh.kfmrh(1, 0));                          // 删 f1，其外环并入 f0 作内环
        const gfx::MeshCounts after = mesh.counts();
        TEST_CHECK(after.faces == before.faces - 1);           // 面 −1
        TEST_CHECK(after.vertices == before.vertices && after.edges == before.edges);
        TEST_CHECK(after.loops == before.loops && after.innerLoops == 1);   // 环总数不变
        TEST_CHECK(!mesh.face(1).alive);                       // donor 面不再存活
        TEST_CHECK(mesh.face(0).loops.size() == 2);            // 接收面：外环 + 内环
        checkValid(mesh, "KFMRH");
    }

    // ---------- 8. 失败路径：非法参数 / 前提不满足 ⇒ 拒绝且不破坏结构 ----------
    {
        HalfEdgeMesh mesh;
        mesh.mvfs(Vec3(0.0f, 0.0f, 0.0f));
        mesh.mevInLoop(0, 0, Vec3(1.0f, 0.0f, 0.0f));
        mesh.mevInLoop(0, 1, Vec3(1.0f, 1.0f, 0.0f));
        mesh.mef(0, 2, 0);
        const gfx::MeshCounts before = mesh.counts();

        TEST_CHECK(mesh.mevInLoop(0, 999, Vec3(0, 0, 0)) == kNoMesh);   // 顶点不存在
        TEST_CHECK(mesh.mef(0, 0, 0) == kNoMesh);                       // v0 == v1
        TEST_CHECK(mesh.mef(999, 0, 1) == kNoMesh);                     // 环不存在
        TEST_CHECK(!mesh.kfmrh(0, 0));                                  // 同一个面
        TEST_CHECK(!mesh.kfmrh(0, 999));                                // 面不存在
        const int e = mesh.edgeBetween(0, 1);                           // 该边两侧属于不同环
        TEST_CHECK(e != kNoMesh && !mesh.kemr(e));
        TEST_CHECK(!mesh.kemr(999));                                    // 边不存在

        const gfx::MeshCounts after = mesh.counts();
        TEST_CHECK(after.vertices == before.vertices && after.edges == before.edges);
        TEST_CHECK(after.faces == before.faces && after.loops == before.loops);

        // 另建一个体（其顶点不属于上面的环）⇒ 用"不在该环中的顶点"调用 MEF 必须被拒绝
        const int otherSolid = mesh.mvfs(Vec3(9.0f, 9.0f, 9.0f));       // 新顶点 id = 3
        TEST_CHECK(otherSolid == 1);
        TEST_CHECK(mesh.mef(0, 3, 0) == kNoMesh);                       // v3 不在环 0 中
        const gfx::MeshCounts last = mesh.counts();
        TEST_CHECK(last.vertices == after.vertices + 1 && last.faces == after.faces + 1);
        TEST_CHECK(last.loops == after.loops + 1 && last.solids == after.solids + 1);
        TEST_CHECK(last.edges == after.edges);
        checkValid(mesh, "失败路径");
    }

    // ---------- 9. 连续调用构造**立方体**并掏空（V=8 E=12 F=6 ⇒ V−E+F=2）----------
    {
        HalfEdgeMesh mesh;
        // 底面 A(0,0,0) B(2,0,0) C(2,2,0) D(0,2,0)
        mesh.mvfs(Vec3(0.0f, 0.0f, 0.0f));
        mesh.mevInLoop(0, 0, Vec3(2.0f, 0.0f, 0.0f));        // B
        mesh.mevInLoop(0, 1, Vec3(2.0f, 2.0f, 0.0f));        // C
        mesh.mevInLoop(0, 2, Vec3(0.0f, 2.0f, 0.0f));        // D
        TEST_CHECK(mesh.mef(0, 3, 0) != kNoMesh);             // 底面（D→A 闭合）
        // 顶面 E/F/G/H（z = 2）
        mesh.mevInLoop(0, 0, Vec3(0.0f, 0.0f, 2.0f));        // E（在 A 处）
        mesh.mevInLoop(0, 4, Vec3(2.0f, 0.0f, 2.0f));        // F（在 E 处）
        TEST_CHECK(mesh.mef(0, 5, 1) != kNoMesh);             // 侧面 1（A,B,F,E）
        // 关键：G 要插在 F 处（这样环顺序才与几何相邻一致，MEF 才能切出四边形侧面）
        mesh.mevInLoop(0, 5, Vec3(2.0f, 2.0f, 2.0f));        // G
        TEST_CHECK(mesh.mef(0, 6, 2) != kNoMesh);             // 侧面 2（B,C,G,F）
        mesh.mevInLoop(0, 6, Vec3(0.0f, 2.0f, 2.0f));        // H
        TEST_CHECK(mesh.mef(0, 7, 3) != kNoMesh);             // 侧面 3（C,D,H,G）
        TEST_CHECK(mesh.mef(0, 7, 4) != kNoMesh);             // 侧面 4（D,A,E,H）⇒ 顶面即 f0

        const gfx::MeshCounts cube = mesh.counts();
        TEST_CHECK(cube.vertices == 8 && cube.edges == 12 && cube.faces == 6);
        TEST_CHECK(cube.loops == 6 && cube.halfEdges == 24 && cube.solids == 1);
        TEST_CHECK(mesh.eulerCharacteristic() == 2);          // 8 − 12 + 6
        checkValid(mesh, "立方体");
        // 立方体每个顶点度数 = 3
        for (int v = 0; v < 8; ++v) {
            int degree = 0;
            for (int e = 0; e < cube.edges; ++e) {
                const gfx::MeshHalfEdge h = mesh.halfEdge(mesh.edge(e).halfEdge);
                if (h.start == v || h.end == v) ++degree;
            }
            TEST_CHECK(degree == 3);
        }

        // ---- 掏空：在顶面（f0 的环）拉"桥 + 内圈"，再删桥 ⇒ 出现**内环（孔）** ----
        mesh.mevInLoop(0, 4, Vec3(0.5f, 0.5f, 2.0f));        // 桥端点 P0
        mesh.mevInLoop(0, 8, Vec3(1.5f, 0.5f, 2.0f));        // P1
        mesh.mevInLoop(0, 9, Vec3(1.5f, 1.5f, 2.0f));        // P2
        mesh.mevInLoop(0, 10, Vec3(0.5f, 1.5f, 2.0f));       // P3
        TEST_CHECK(mesh.mef(0, 11, 8) != kNoMesh);            // 闭合内圈
        const int bridge = mesh.edgeBetween(4, 8);
        TEST_CHECK(bridge != kNoMesh);
        const int bh1 = mesh.edge(bridge).halfEdge;
        TEST_CHECK(mesh.halfEdge(bh1).loop == mesh.halfEdge(mesh.halfEdge(bh1).opposite).loop);  // 桥接边前提
        TEST_CHECK(mesh.kemr(bridge));

        const gfx::MeshCounts hollow = mesh.counts();
        TEST_CHECK(hollow.innerLoops == 1);                   // 出现了内环（孔）
        TEST_CHECK(hollow.vertices == 12 && hollow.faces == 7);
        TEST_CHECK(hollow.edges == cube.edges + 5 - 1);       // +5 条边（内圈 4 + 桥 1）−1（删桥）
        checkValid(mesh, "立方体掏空");
    }

    // ---------- 10. MEFR：在两圈"平行环"之间缝出一圈四边形面（跨环建面）----------
    {
        HalfEdgeMesh mesh;
        // -- 立方体：底面 A/B/C/D（z=0），顶面 E/F/G/H（z=2）（序列同用例 9）--
        mesh.mvfs(Vec3(0.0f, 0.0f, 0.0f));                   // A = v0
        mesh.mevInLoop(0, 0, Vec3(2.0f, 0.0f, 0.0f));        // B = v1
        mesh.mevInLoop(0, 1, Vec3(2.0f, 2.0f, 0.0f));        // C = v2
        mesh.mevInLoop(0, 2, Vec3(0.0f, 2.0f, 0.0f));        // D = v3
        TEST_CHECK(mesh.mef(0, 3, 0) != kNoMesh);             // 底面
        mesh.mevInLoop(0, 0, Vec3(0.0f, 0.0f, 2.0f));        // E = v4
        mesh.mevInLoop(0, 4, Vec3(2.0f, 0.0f, 2.0f));        // F = v5
        TEST_CHECK(mesh.mef(0, 5, 1) != kNoMesh);             // 侧面 1
        mesh.mevInLoop(0, 5, Vec3(2.0f, 2.0f, 2.0f));        // G = v6
        TEST_CHECK(mesh.mef(0, 6, 2) != kNoMesh);             // 侧面 2
        mesh.mevInLoop(0, 6, Vec3(0.0f, 2.0f, 2.0f));        // H = v7
        TEST_CHECK(mesh.mef(0, 7, 3) != kNoMesh);             // 侧面 3
        TEST_CHECK(mesh.mef(0, 7, 4) != kNoMesh);             // 侧面 4 ⇒ 顶面 f0 / 底面 f1
        TEST_CHECK(mesh.counts().vertices == 8 && mesh.counts().faces == 6);
        checkValid(mesh, "MEFR-立方体");

        // -- 在某面挖出**两个内环**：桥 + 内圈 + MEF（盖子）+ KEMR（删桥）+ KFMRH（盖子并回）--
        const auto digDoubleInnerLoop = [&](gfx::MeshId faceId, float x0, float y0, float x1, float y1, float z) {
            const gfx::MeshId loop = mesh.face(faceId).outerLoop;
            const std::vector<gfx::MeshId> lv = mesh.loopVertices(loop);
            if (lv.empty()) return false;
            const gfx::MeshId anchor = lv[0];
            const gfx::MeshId p0 = mesh.mevInLoop(loop, anchor, Vec3(x0, y0, z));
            const gfx::MeshId p1 = mesh.mevInLoop(loop, p0, Vec3(x1, y0, z));
            const gfx::MeshId p2 = mesh.mevInLoop(loop, p1, Vec3(x1, y1, z));
            const gfx::MeshId p3 = mesh.mevInLoop(loop, p2, Vec3(x0, y1, z));
            if (p0 == kNoMesh || p1 == kNoMesh || p2 == kNoMesh || p3 == kNoMesh) return false;
            const gfx::MeshId cap = mesh.mef(loop, p3, p0);
            if (cap == kNoMesh) return false;
            const gfx::MeshId bridge = mesh.edgeBetween(anchor, p0);
            if (bridge == kNoMesh || !mesh.kemr(bridge)) return false;
            return mesh.kfmrh(cap, faceId);
        };
        TEST_CHECK(digDoubleInnerLoop(0, 0.5f, 0.5f, 1.5f, 1.5f, 2.0f));   // 顶面两个内环
        TEST_CHECK(digDoubleInnerLoop(1, 0.5f, 0.5f, 1.5f, 1.5f, 0.0f));   // 底面两个内环
        checkValid(mesh, "MEFR-挖双内环");

        const auto innerLoopsOf = [&](gfx::MeshId f) {
            std::vector<gfx::MeshId> out;
            for (gfx::MeshId l : mesh.face(f).loops) if (!mesh.loop(l).isOuter) out.push_back(l);
            return out;
        };
        const std::vector<gfx::MeshId> topInner = innerLoopsOf(0);
        const std::vector<gfx::MeshId> botInner = innerLoopsOf(1);
        TEST_CHECK(topInner.size() == 2 && botInner.size() == 2);
        TEST_CHECK(mesh.counts().innerLoops == 4);
        TEST_CHECK(mesh.counts().vertices == 16);             // 立方体 8 + 上/下各 4 个内圈点
        checkValid(mesh, "MEFR-挖双内环后");

        // -- 记录两环顶点集合（用于核对新面顶点来源）；并确认两环之间尚无竖棱 --
        const gfx::MeshId loopA = topInner[0];
        const gfx::MeshId loopB = botInner[0];
        const std::vector<gfx::MeshId> ringTop = mesh.loopVertices(loopA);
        const std::vector<gfx::MeshId> ringBot = mesh.loopVertices(loopB);
        const int n = static_cast<int>(ringTop.size());
        TEST_CHECK(n == 4 && static_cast<int>(ringBot.size()) == 4);
        TEST_CHECK(mesh.edgeBetween(ringTop[0], ringBot[0]) == kNoMesh);   // 竖棱此刻不存在

        const gfx::MeshCounts before = mesh.counts();
        const gfx::MeshId firstNew = mesh.mefr(loopA, loopB);
        TEST_CHECK(firstNew != kNoMesh);
        const gfx::MeshCounts after = mesh.counts();

        // -- 数量断言：V 不变；E+N；F+N；L+(N−2)；内环 −2（两环被消费）--
        TEST_CHECK(after.vertices == before.vertices);
        TEST_CHECK(after.edges == before.edges + n);
        TEST_CHECK(after.faces == before.faces + n);
        TEST_CHECK(after.loops == before.loops + n - 2);
        TEST_CHECK(after.halfEdges == before.halfEdges + 2 * n);   // 只有 N 条竖棱是新建（各 2 半边）
        TEST_CHECK(after.innerLoops == before.innerLoops - 2);

        // -- 被消费的两个环：不再存活，也不再出现在任何面里 --
        TEST_CHECK(!mesh.loop(loopA).alive && !mesh.loop(loopB).alive);
        mesh.forEachFace([&](gfx::MeshId f) {
            for (gfx::MeshId l : mesh.face(f).loops) {
                TEST_CHECK(l != loopA && l != loopB);
                TEST_CHECK(mesh.loop(l).alive);
            }
        });

        // -- N 个新面：外环都是 4 顶点，且 4 顶点分别来自上、下两环 --
        const auto inRing = [](const std::vector<gfx::MeshId>& ring, gfx::MeshId v) {
            for (gfx::MeshId x : ring) if (x == v) return true;
            return false;
        };
        for (int k = 0; k < n; ++k) {
            const gfx::MeshId f = firstNew + k;
            TEST_CHECK(mesh.face(f).alive);
            const gfx::MeshId ol = mesh.face(f).outerLoop;
            TEST_CHECK(mesh.loop(ol).isOuter);                    // 新面外环
            const std::vector<gfx::MeshId> fv = mesh.loopVertices(ol);
            TEST_CHECK(fv.size() == 4);
            int fromTop = 0, fromBot = 0;
            for (gfx::MeshId v : fv) {
                if (inRing(ringTop, v)) ++fromTop;
                if (inRing(ringBot, v)) ++fromBot;
            }
            TEST_CHECK(fromTop == 2 && fromBot == 2);
        }

        checkValid(mesh, "MEFR");
        // 贯通孔（亏格 1）：V−E+F = 2，而 V−E+F−内环数 = 0
        TEST_CHECK(mesh.eulerCharacteristic() == 2);
        TEST_CHECK(mesh.eulerCharacteristic() - after.innerLoops == 0);

        // -- 失败路径：非法环 / 已消费环 / 自身 ⇒ 拒绝且结构不变 --
        {
            const gfx::MeshCounts c0 = mesh.counts();
            TEST_CHECK(mesh.mefr(999, botInner[1]) == kNoMesh);       // loopA 不存在
            TEST_CHECK(mesh.mefr(loopA, topInner[1]) == kNoMesh);     // loopA 已被消费
            TEST_CHECK(mesh.mefr(topInner[1], topInner[1]) == kNoMesh);  // 同一个环
            const gfx::MeshCounts c1 = mesh.counts();
            TEST_CHECK(c1.faces == c0.faces && c1.edges == c0.edges && c1.loops == c0.loops);
        }
        checkValid(mesh, "MEFR-失败路径");
    }

    return gfx::test::summary();
}
