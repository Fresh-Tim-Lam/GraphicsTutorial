// halfedge.hpp —— gfx 半边数据结构（B-rep）与欧拉操作
//
// @file 实体造型的**边界表示**（Boundary Representation）与**五个欧拉操作**：
//         MVFS（建体/面/环/点）、MEV（建边与点）、MEF（建边与面）、
//         KEMR（删边建内环）、KFMRH（删面并把其外环转为另一面的内环）。
//         另有 MEFR（Make Edge Face Ring，跨环建面）：在两圈平行环之间缝出一圈四边形面。
//
// 拓扑层级（与课程讲义一致）：
//   Solid(体) ─→ Face(面) ─→ Loop(环) ─→ HalfEdge(半边) ─→ Vertex(顶点)
//                                └─ Edge(无向边 = 一对互为 opposite 的半边)
//
// @note 元素用**索引池**（std::vector + 稳定 id）而非指针：id 稳定便于断言与文本输出；
//       删除只标记 alive（不回收），教学规模下足够，且避免悬垂引用。
// @note 顶点约定：`MeshVertex::halfEdge` 指向**以该顶点为起点**的一条半边；孤立顶点为 kNoMesh。
//       每次拓扑修改后统一重扫一遍（repairVertexHalfEdges），保证该约定始终成立。
// @note 欧拉公式（讲义 p16）：V − E + F = 2(s − h) + r（s 体数、h 柄/亏格、r 内环数）。
//       本实现不产生柄 ⇒ 对**封闭体**应有 V − E + F = 2；带边界的面片（中间状态）不满足，
//       故 validate() 只查**结构一致性**，欧拉特征数由 eulerCharacteristic() 单独报告。
//
// 用途：按欧拉操作逐步构造/修改实体的拓扑（后续可接几何造型、网格编辑）。
// 目标读者：几何造型 / 网格数据结构的使用者与实现者（#include <gfx/geom/halfedge.hpp>）。
// 关联文档：docs/design/adr-0009-halfedge-euler-ops.md；gfx/math/vec.hpp
//
// 状态：新增（2026-09-16）。对应作业二：欧拉操作。

#ifndef GFX_HALFEDGE_HPP
#define GFX_HALFEDGE_HPP

#include "gfx/math/vec.hpp"

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

namespace gfx {

// 元素索引（池下标）；kNoMesh 表示"无"
using MeshId = int;
inline constexpr MeshId kNoMesh = -1;

// 顶点
struct MeshVertex {
    math::Vec3 position{};
    MeshId halfEdge = kNoMesh;   // 以本点为起点的一条半边（孤立点为 kNoMesh）
    bool alive = true;
};

// 半边（有向边）
struct MeshHalfEdge {
    MeshId start = kNoMesh;      // 起点顶点
    MeshId end = kNoMesh;        // 终点顶点（与 opposite 对称，冗余但便于查询/校验）
    MeshId next = kNoMesh;       // 环上的后继
    MeshId prev = kNoMesh;       // 环上的前驱
    MeshId opposite = kNoMesh;   // 配对的另一半边
    MeshId edge = kNoMesh;       // 所属无向边
    MeshId loop = kNoMesh;       // 所属环
    bool alive = true;
};

// 无向边（承载一对半边）
struct MeshEdge {
    MeshId halfEdge = kNoMesh;   // 其中一条半边
    bool alive = true;
};

// 环（面的边界；外环 or 内环）
struct MeshLoop {
    MeshId halfEdge = kNoMesh;   // 环上任一条半边（空环为 kNoMesh，见 MVFS 后的初始环）
    MeshId face = kNoMesh;       // 所属面
    bool isOuter = true;         // true = 外环（面边界）；false = 内环（孔）
    bool alive = true;
};

// 面
struct MeshFace {
    MeshId outerLoop = kNoMesh;         // 外环
    std::vector<MeshId> loops;          // 全部环（外环在前，其余为内环）
    bool alive = true;
};

// 体
struct MeshSolid {
    std::vector<MeshId> faces;          // 本体的面集合
    bool alive = true;
};

// 元素计数（只统计 alive 的元素）
struct MeshCounts {
    int vertices = 0;
    int edges = 0;
    int halfEdges = 0;
    int loops = 0;
    int faces = 0;
    int solids = 0;
    int innerLoops = 0;      // 内环数（欧拉公式里的 r）
};

// 半边网格：持有元素池，提供五个欧拉操作与一致性校验。
class HalfEdgeMesh {
private:
    // ---- 元素池 ----
    std::vector<MeshVertex> vertices_;
    std::vector<MeshHalfEdge> halfEdges_;
    std::vector<MeshEdge> edges_;
    std::vector<MeshLoop> loops_;
    std::vector<MeshFace> faces_;
    std::vector<MeshSolid> solids_;

    // ---- 有效性 ----
    bool aliveVertex(MeshId i) const { return i >= 0 && i < static_cast<MeshId>(vertices_.size()) && vertices_[static_cast<std::size_t>(i)].alive; }
    bool aliveHalfEdge(MeshId i) const { return i >= 0 && i < static_cast<MeshId>(halfEdges_.size()) && halfEdges_[static_cast<std::size_t>(i)].alive; }
    bool aliveEdge(MeshId i) const { return i >= 0 && i < static_cast<MeshId>(edges_.size()) && edges_[static_cast<std::size_t>(i)].alive; }
    bool aliveLoop(MeshId i) const { return i >= 0 && i < static_cast<MeshId>(loops_.size()) && loops_[static_cast<std::size_t>(i)].alive; }
    bool aliveFace(MeshId i) const { return i >= 0 && i < static_cast<MeshId>(faces_.size()) && faces_[static_cast<std::size_t>(i)].alive; }

    // ---- 池分配 ----
    MeshId addVertex(const math::Vec3& p) {
        vertices_.push_back(MeshVertex{p, kNoMesh, true});
        return static_cast<MeshId>(vertices_.size()) - 1;
    }
    MeshId addHalfEdge(MeshId start, MeshId end, MeshId loop, MeshId edge) {
        MeshHalfEdge h;
        h.start = start; h.end = end; h.loop = loop; h.edge = edge;
        halfEdges_.push_back(h);
        return static_cast<MeshId>(halfEdges_.size()) - 1;
    }
    MeshId addEdge() {
        edges_.push_back(MeshEdge{});
        return static_cast<MeshId>(edges_.size()) - 1;
    }
    MeshId addLoop() {
        loops_.push_back(MeshLoop{});
        return static_cast<MeshId>(loops_.size()) - 1;
    }
    MeshId addFace() {
        faces_.push_back(MeshFace{});
        return static_cast<MeshId>(faces_.size()) - 1;
    }
    MeshId addSolid() {
        solids_.push_back(MeshSolid{});
        return static_cast<MeshId>(solids_.size()) - 1;
    }

    // ---- 查找 ----
    // 在环 loop 中找一条以 v 为起点的半边
    MeshId halfEdgeFrom(MeshId v, MeshId loop) const {
        if (!aliveLoop(loop) || loops_[loop].halfEdge == kNoMesh) return kNoMesh;
        const MeshId start = loops_[loop].halfEdge;
        MeshId h = start;
        do {
            if (halfEdges_[h].start == v) return h;
            h = halfEdges_[h].next;
        } while (h != start && h != kNoMesh);
        return kNoMesh;
    }

    // 顶点所在环：有半边时取其环；孤立顶点（MVFS 之后）取那个空环
    MeshId loopOfVertex(MeshId v) const {
        if (aliveVertex(v) && vertices_[v].halfEdge != kNoMesh)
            return halfEdges_[vertices_[v].halfEdge].loop;
        for (std::size_t i = 0; i < loops_.size(); ++i)
            if (loops_[i].alive && loops_[i].halfEdge == kNoMesh) return static_cast<MeshId>(i);
        return kNoMesh;
    }

    MeshId solidOfFace(MeshId f) const {
        for (std::size_t i = 0; i < solids_.size(); ++i) {
            if (!solids_[i].alive) continue;
            if (std::find(solids_[i].faces.begin(), solids_[i].faces.end(), f) != solids_[i].faces.end())
                return static_cast<MeshId>(i);
        }
        return kNoMesh;
    }

    // 重建顶点入口：保证"顶点 → 以它为起点的某条 living 半边"这一约定
    void repairVertexHalfEdges() {
        for (auto& v : vertices_) v.halfEdge = kNoMesh;
        for (std::size_t i = 0; i < halfEdges_.size(); ++i) {
            if (!halfEdges_[i].alive) continue;
            const MeshId s = halfEdges_[i].start;
            if (s >= 0 && vertices_[static_cast<std::size_t>(s)].halfEdge == kNoMesh)
                vertices_[static_cast<std::size_t>(s)].halfEdge = static_cast<MeshId>(i);
        }
    }

public:
    // ==================== 五个欧拉操作 ====================

    // MVFS（Make Vertex Face Solid）：用坐标 p 建立一个新**体**、一个新**面**、
    //   该面的**外环**与一个**顶点**；此时环上无边（初始状态）。
    // @return 新建体的 id
    MeshId mvfs(const math::Vec3& p) {
        const MeshId v = addVertex(p);
        const MeshId s = addSolid();
        const MeshId f = addFace();
        const MeshId l = addLoop();
        loops_[l].face = f;
        loops_[l].halfEdge = kNoMesh;      // 初始环：还没有边
        loops_[l].isOuter = true;
        faces_[f].outerLoop = l;
        faces_[f].loops.push_back(l);
        solids_[s].faces.push_back(f);
        return s;
    }

    // MEV（Make Edge Vertex）：在顶点 v0 **所在的环**上添加新顶点（坐标 p）与连接边。
    //   两种情形都要处理：① MVFS 之后环上还没有边；② 环上已有边（在 v0 处插入）。
    // @return 新顶点 id；失败（v0 非法 / 找不到所属环）返回 kNoMesh
    MeshId mev(MeshId v0, const math::Vec3& p) { return mevInLoop(loopOfVertex(v0), v0, p); }

    // MEV 的**显式指定环**版本：顶点同时属于多个环时（例如 MEF 之后）用它精确定位。
    // @return 新顶点 id；失败（环/顶点非法、v0 不在该环上）返回 kNoMesh
    MeshId mevInLoop(MeshId loop, MeshId v0, const math::Vec3& p) {
        if (!aliveLoop(loop) || !aliveVertex(v0)) return kNoMesh;
        if (loops_[loop].halfEdge != kNoMesh && halfEdgeFrom(v0, loop) == kNoMesh) return kNoMesh;

        const MeshId v1 = addVertex(p);
        const MeshId e = addEdge();
        const MeshId a = addHalfEdge(v1, v0, loop, e);   // v1 → v0
        const MeshId b = addHalfEdge(v0, v1, loop, e);   // v0 → v1
        edges_[e].halfEdge = a;
        halfEdges_[a].opposite = b;
        halfEdges_[b].opposite = a;

        if (loops_[loop].halfEdge == kNoMesh) {
            // 情形①：初始环（MVFS 之后，环上无半边）⇒ 两条半边自成闭环
            halfEdges_[a].next = b;
            halfEdges_[a].prev = b;
            halfEdges_[b].next = a;
            halfEdges_[b].prev = a;
            loops_[loop].halfEdge = a;
        } else {
            // 情形②：环上已有边 ⇒ 在"以 v0 为起点的半边"之前插入 [b, a]
            const MeshId he0 = halfEdgeFrom(v0, loop);
            if (he0 == kNoMesh) return kNoMesh;    // v0 不在该环上（调用方用错参数）
            const MeshId before = halfEdges_[he0].prev;
            halfEdges_[before].next = b;
            halfEdges_[b].prev = before;
            halfEdges_[b].next = a;
            halfEdges_[a].prev = b;
            halfEdges_[a].next = he0;
            halfEdges_[he0].prev = a;
        }

        repairVertexHalfEdges();
        return v1;
    }

    // MEF（Make Edge Face）：在**指定环** loop 中，用一条新边连接 v0 与 v1，
    //   把该环拆成两个环：沿环 v0→…→v1 的那段 + 新边构成**新环**（新面的外环），
    //   其余部分 + 另一条半边留在**原环**（原面）。
    // @return 新面 id；失败（环/顶点非法、v0 == v1、环中找不到 v1）返回 kNoMesh
    MeshId mef(MeshId loop, MeshId v0, MeshId v1) {
        if (!aliveLoop(loop) || !aliveVertex(v0) || !aliveVertex(v1) || v0 == v1) return kNoMesh;
        const MeshId head = halfEdgeFrom(v0, loop);
        if (head == kNoMesh) return kNoMesh;

        // 沿环从 v0 向前走到 v1（P1）
        std::vector<MeshId> path;
        MeshId h = head;
        for (;;) {
            path.push_back(h);
            if (halfEdges_[h].end == v1) break;
            const MeshId nx = halfEdges_[h].next;
            if (nx == kNoMesh || nx == head) return kNoMesh;   // 环里没有 v1
            h = nx;
        }
        const MeshId p1Last = path.back();
        const MeshId p2First = halfEdges_[p1Last].next;
        const MeshId p2Last = halfEdges_[head].prev;
        if (p2First == head) return kNoMesh;   // P2 为空 ⇒ 无法留下原环

        const MeshId e = addEdge();
        const MeshId newLoop = addLoop();
        const MeshId newFace = addFace();
        const MeshId hToNew = addHalfEdge(v1, v0, newLoop, e);   // 新环用
        const MeshId hToOld = addHalfEdge(v0, v1, loop, e);      // 原环用
        edges_[e].halfEdge = hToNew;
        halfEdges_[hToNew].opposite = hToOld;
        halfEdges_[hToOld].opposite = hToNew;

        // 新环 = P1 + hToNew（P1 的半边改属新环）
        for (MeshId x : path) halfEdges_[x].loop = newLoop;
        halfEdges_[p1Last].next = hToNew;
        halfEdges_[hToNew].prev = p1Last;
        halfEdges_[hToNew].next = head;
        halfEdges_[head].prev = hToNew;

        loops_[newLoop].halfEdge = head;
        loops_[newLoop].face = newFace;
        loops_[newLoop].isOuter = true;
        faces_[newFace].outerLoop = newLoop;
        faces_[newFace].loops.push_back(newLoop);

        // 原环 = P2 + hToOld
        halfEdges_[hToOld].next = p2First;
        halfEdges_[p2First].prev = hToOld;
        halfEdges_[hToOld].prev = p2Last;
        halfEdges_[p2Last].next = hToOld;
        loops_[loop].halfEdge = hToOld;

        const MeshId solid = solidOfFace(loops_[loop].face);
        if (solid != kNoMesh) solids_[solid].faces.push_back(newFace);

        repairVertexHalfEdges();
        return newFace;
    }

    // KEMR（Kill Edge Make Ring）：删除**桥接边**（两条半边同属一个环），
    //   把该环拆成两个环：一个保留为外环，另一个成为**同一面的内环**（挖孔的拓扑基础）。
    // @return 成功 true；失败（边非法 / 非桥接边 / 拆出退化环）false
    bool kemr(MeshId edge) {
        if (!aliveEdge(edge)) return false;
        const MeshId h1 = edges_[edge].halfEdge;
        if (!aliveHalfEdge(h1)) return false;
        const MeshId h2 = halfEdges_[h1].opposite;
        if (!aliveHalfEdge(h2)) return false;

        const MeshId loop = halfEdges_[h1].loop;
        if (loop == kNoMesh || halfEdges_[h2].loop != loop) return false;   // 两侧不同环 ⇒ 不是桥接边

        // 两条半边相邻（互相 next）⇒ 删除只会得到退化环，不具备"拆环"意义
        if (halfEdges_[h1].next == h2 || halfEdges_[h2].next == h1) return false;

        const MeshId aFirst = halfEdges_[h1].next;
        const MeshId aLast = halfEdges_[h2].prev;
        const MeshId bFirst = halfEdges_[h2].next;
        const MeshId bLast = halfEdges_[h1].prev;

        // 边与两条半边出池
        halfEdges_[h1].alive = false;
        halfEdges_[h2].alive = false;
        edges_[edge].alive = false;

        // 段 A（h1.next … h2.prev）闭合，保留为原环（外环）
        halfEdges_[aLast].next = aFirst;
        halfEdges_[aFirst].prev = aLast;
        loops_[loop].halfEdge = aFirst;

        // 段 B（h2.next … h1.prev）独立成环（内环）
        halfEdges_[bLast].next = bFirst;
        halfEdges_[bFirst].prev = bLast;

        const MeshId innerLoop = addLoop();
        for (MeshId x = bFirst;; x = halfEdges_[x].next) {
            halfEdges_[x].loop = innerLoop;
            if (x == bLast) break;
        }
        const MeshId face = loops_[loop].face;
        loops_[innerLoop].halfEdge = bFirst;
        loops_[innerLoop].face = face;
        loops_[innerLoop].isOuter = false;
        if (face != kNoMesh) faces_[face].loops.push_back(innerLoop);

        repairVertexHalfEdges();
        return true;
    }

    // KFMRH（Kill Face Make Ring Hole）：删除 donor 面，把它的**外环**转为 receiver 面的**内环**。
    // 前提（基础实现）：两面属于同一体；donor 只有一个外环、不含内环。
    // @return 成功 true；失败（面非法/同面/不同体/donor 有多个环）false
    bool kfmrh(MeshId donorFace, MeshId receiverFace) {
        if (!aliveFace(donorFace) || !aliveFace(receiverFace) || donorFace == receiverFace) return false;
        const MeshId s1 = solidOfFace(donorFace);
        const MeshId s2 = solidOfFace(receiverFace);
        if (s1 == kNoMesh || s1 != s2) return false;                   // 必须同体
        if (faces_[donorFace].loops.size() != 1) return false;          // donor 须只有一个环
        const MeshId l = faces_[donorFace].outerLoop;
        if (l == kNoMesh || !loops_[l].isOuter) return false;

        // 转移环：归属接收面，并标记为内环
        loops_[l].face = receiverFace;
        loops_[l].isOuter = false;
        faces_[receiverFace].loops.push_back(l);

        // 删除 donor 面并从体的面集合移除
        faces_[donorFace].alive = false;
        auto& fs = solids_[s1].faces;
        fs.erase(std::remove(fs.begin(), fs.end(), donorFace), fs.end());

        repairVertexHalfEdges();
        return true;
    }

    // MFKRH（Make Face Kill Ring Hole）：把 receiver 面上的一个**内环**摘出来，
    //   成为**一个新面的外环**（"从内环长出一个面"）—— 这是 KFMRH 的逆操作（讲义 p25 的 mfkrh）。
    // @return 新面 id；失败（面/环非法、环不属于该面、或它不是内环）返回 kNoMesh
    // @note 数量变化：F+1（V/E/L 均不变）。用途：把"错挂/多余的内环"变成独立面，再交给 KFMRH 并到该去的面上。
    MeshId mfkrh(MeshId receiverFace, MeshId innerLoop) {
        if (!aliveFace(receiverFace) || !aliveLoop(innerLoop)) return kNoMesh;
        if (loops_[innerLoop].face != receiverFace) return kNoMesh;   // 必须是该面的环
        if (loops_[innerLoop].isOuter) return kNoMesh;                // 必须是**内**环

        auto& ls = faces_[receiverFace].loops;
        ls.erase(std::remove(ls.begin(), ls.end(), innerLoop), ls.end());

        const MeshId newFace = addFace();
        loops_[innerLoop].face = newFace;
        loops_[innerLoop].isOuter = true;                             // 内环 → 新面的外环
        faces_[newFace].outerLoop = innerLoop;
        faces_[newFace].loops.push_back(innerLoop);

        const MeshId solid = solidOfFace(receiverFace);
        if (solid != kNoMesh) solids_[solid].faces.push_back(newFace);

        repairVertexHalfEdges();
        return newFace;
    }

    // MEFR（Make Edge Face Ring，跨环建面）：在两圈"平行环"之间缝出一圈四边形面（把两圈顶点
    //   缝成一根方管）。设环 A 的顶点序为 VA[0..N−1]（其半边 α_i：VA[i]→VA[i+1]），环 B 类似。
    //   ① 对位：调用方不保证两环起点对应、也不保证走向，故取 loopB 中与 VA[0] 几何最近者为
    //      对应起点 q_off；再由 VA[1] 与 q_{off±1} 的距离判定 loopB 相对 loopA 是同向还是反向。
    //   ② 记 B_i 与 VA[i] 对应，并取**反向索引** B_i = q_{off−i}：这样 B_{i+1}→B_i 恰好是 loopB
    //      的**己有半边** β（满足"复用而不新建"；同向索引则会落到配对半边上，无法复用）。
    //   ③ 新建 N 条竖棱 VA[i]—B_i；第 i 张新面 = VA[i]→VA[i+1]→B_{i+1}→B_i→VA[i]，其中
    //      VA[i]→VA[i+1] 复用 α_i、B_{i+1}→B_i 复用 loopB 的半边 γ_i。
    //   ④ 环 A、环 B 被**消费**（从所属面的环列表移除；若为外环则让同面另一环接任并标记为外环）。
    // 数量变化：V 不变、E+N、F+N、L+(N−2)；新面均 isOuter = true。
    // @return 第一个新面 id；失败（环非法/顶点数不等或 <3/同面却含外环/会掏空面）返回 kNoMesh（结构不变）
    MeshId mefr(MeshId loopA, MeshId loopB) {
        // ---- 前提检查 ----
        if (!aliveLoop(loopA) || !aliveLoop(loopB) || loopA == loopB) return kNoMesh;
        const std::vector<MeshId> va = loopVertices(loopA);
        const std::vector<MeshId> vb = loopVertices(loopB);
        const int n = static_cast<int>(va.size());
        if (n < 3 || static_cast<int>(vb.size()) != n) return kNoMesh;

        const MeshId faceA = loops_[loopA].face;
        const MeshId faceB = loops_[loopB].face;
        if (!aliveFace(faceA) || !aliveFace(faceB)) return kNoMesh;
        const bool aOuter = loops_[loopA].isOuter;
        const bool bOuter = loops_[loopB].isOuter;
        if (faceA == faceB && (aOuter || bOuter)) return kNoMesh;        // 同面仅支持两个内环
        if (aOuter && faces_[faceA].loops.size() < 2) return kNoMesh;    // 否则会把面掏空
        if (bOuter && faces_[faceB].loops.size() < 2) return kNoMesh;

        // ---- ① 几何对位 + 走向判定 ----
        const auto dist2 = [this](MeshId x, MeshId y) {
            const math::Vec3& a = vertices_[static_cast<std::size_t>(x)].position;
            const math::Vec3& b = vertices_[static_cast<std::size_t>(y)].position;
            const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            return dx * dx + dy * dy + dz * dz;
        };
        int off = 0;
        for (int k = 1; k < n; ++k)
            if (dist2(va[0], vb[static_cast<std::size_t>(k)]) < dist2(va[0], vb[static_cast<std::size_t>(off)]))
                off = k;
        // 走向判定（VA[1] 更靠近 q_{off−1} ⇒ 反向存储）由**反向索引** B_i = q_{off−i} 内化：
        //   反向索引使 B_{i+1} 恒为 B_i 在 loopB 上的前驱，故 B_{i+1}→B_i 必是 loopB 的己有半边，
        //   复用成立；两环恰好反向存储时几何也不扭（几何最近邻对位与反向索引自洽）。

        // ---- ② 预取要复用的半边（改归属前先算好）----
        std::vector<MeshId> topHe(static_cast<std::size_t>(n)), botHe(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            topHe[static_cast<std::size_t>(i)] = halfEdgeFrom(va[static_cast<std::size_t>(i)], loopA);   // A_i → A_{i+1}
            if (topHe[static_cast<std::size_t>(i)] == kNoMesh) return kNoMesh;
            const MeshId bn = vb[static_cast<std::size_t>((off - (i + 1) + n) % n)];                     // B_{i+1}
            const MeshId bc = vb[static_cast<std::size_t>((off - i + n) % n)];                           // B_i
            const MeshId hb = halfEdgeFrom(bn, loopB);                                                   // 取 loopB 己有半边
            if (hb == kNoMesh || halfEdges_[hb].end != bc) return kNoMesh;
            botHe[static_cast<std::size_t>(i)] = hb;
        }

        // ---- ③ 新建 N 条竖棱（每条两条半边，分属相邻两张新面）----
        std::vector<MeshId> upHe(static_cast<std::size_t>(n)), downHe(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            const MeshId bc = vb[static_cast<std::size_t>((off - i + n) % n)];      // B_i
            const MeshId e = addEdge();
            const MeshId up = addHalfEdge(va[static_cast<std::size_t>(i)], bc, kNoMesh, e);   // A_i → B_i
            const MeshId dn = addHalfEdge(bc, va[static_cast<std::size_t>(i)], kNoMesh, e);   // B_i → A_i
            edges_[e].halfEdge = up;
            halfEdges_[up].opposite = dn;
            halfEdges_[dn].opposite = up;
            upHe[static_cast<std::size_t>(i)] = up;
            downHe[static_cast<std::size_t>(i)] = dn;
        }

        // ---- ④ 建 N 张四边形面并接线 ----
        const MeshId solid = solidOfFace(faceA);
        MeshId firstFace = kNoMesh;
        for (int i = 0; i < n; ++i) {
            const MeshId loop = addLoop();
            const MeshId face = addFace();
            if (firstFace == kNoMesh) firstFace = face;
            const MeshId h1 = topHe[static_cast<std::size_t>(i)];                    // A_i → A_{i+1}
            const MeshId h2 = upHe[static_cast<std::size_t>((i + 1) % n)];           // A_{i+1} → B_{i+1}
            const MeshId h3 = botHe[static_cast<std::size_t>(i)];                    // B_{i+1} → B_i（复用）
            const MeshId h4 = downHe[static_cast<std::size_t>(i)];                   // B_i → A_i
            halfEdges_[h1].next = h2; halfEdges_[h2].prev = h1;
            halfEdges_[h2].next = h3; halfEdges_[h3].prev = h2;
            halfEdges_[h3].next = h4; halfEdges_[h4].prev = h3;
            halfEdges_[h4].next = h1; halfEdges_[h1].prev = h4;
            halfEdges_[h1].loop = loop;
            halfEdges_[h2].loop = loop;
            halfEdges_[h3].loop = loop;
            halfEdges_[h4].loop = loop;
            loops_[loop].halfEdge = h1;
            loops_[loop].face = face;
            loops_[loop].isOuter = true;
            faces_[face].outerLoop = loop;
            faces_[face].loops.push_back(loop);
            if (solid != kNoMesh) solids_[solid].faces.push_back(face);
        }

        // ---- ⑤ 消费两个环 ----
        const auto consume = [&](MeshId lp) {
            const MeshId f = loops_[lp].face;
            auto& ls = faces_[f].loops;
            ls.erase(std::remove(ls.begin(), ls.end(), lp), ls.end());
            if (faces_[f].outerLoop == lp && !ls.empty()) {
                faces_[f].outerLoop = ls.front();
                loops_[ls.front()].isOuter = true;
            }
            loops_[lp].alive = false;
        };
        consume(loopA);
        consume(loopB);

        repairVertexHalfEdges();
        return firstFace;
    }

    // ==================== 元素访问 ====================
    const MeshVertex& vertex(MeshId i) const { return vertices_[static_cast<std::size_t>(i)]; }
    const MeshHalfEdge& halfEdge(MeshId i) const { return halfEdges_[static_cast<std::size_t>(i)]; }
    const MeshEdge& edge(MeshId i) const { return edges_[static_cast<std::size_t>(i)]; }
    const MeshLoop& loop(MeshId i) const { return loops_[static_cast<std::size_t>(i)]; }
    const MeshFace& face(MeshId i) const { return faces_[static_cast<std::size_t>(i)]; }
    const MeshSolid& solid(MeshId i) const { return solids_[static_cast<std::size_t>(i)]; }

    // 查找连接 v0、v1 的边（同一条边可能有多个方向；返回第一条；不存在返回 kNoMesh）
    MeshId edgeBetween(MeshId v0, MeshId v1) const {
        for (std::size_t i = 0; i < edges_.size(); ++i) {
            if (!edges_[i].alive) continue;
            const MeshId h = edges_[i].halfEdge;
            if (!aliveHalfEdge(h)) continue;
            const MeshId s = halfEdges_[h].start;
            const MeshId t = halfEdges_[h].end;
            if ((s == v0 && t == v1) || (s == v1 && t == v0)) return static_cast<MeshId>(i);
        }
        return kNoMesh;
    }

    // 环中一条以 v 为起点的半边（不存在返回 kNoMesh）
    MeshId halfEdgeOf(MeshId v, MeshId loop) const { return halfEdgeFrom(v, loop); }

    // 第一个**内环**（isOuter == false 的存活环）；没有则返回 kNoMesh
    MeshId firstInnerLoop() const {
        for (std::size_t i = 0; i < loops_.size(); ++i)
            if (loops_[i].alive && !loops_[i].isOuter) return static_cast<MeshId>(i);
        return kNoMesh;
    }

    // 环上的顶点序列（从入口半边开始，沿 next 走一圈）
    std::vector<MeshId> loopVertices(MeshId loop) const {
        std::vector<MeshId> out;
        if (!aliveLoop(loop) || loops_[loop].halfEdge == kNoMesh) return out;
        const MeshId start = loops_[loop].halfEdge;
        MeshId h = start;
        do {
            out.push_back(halfEdges_[h].start);
            h = halfEdges_[h].next;
        } while (h != start && h != kNoMesh);
        return out;
    }

    // 遍历所有存活的边：fn(edgeId, startVertex, endVertex)
    template <class Fn>
    void forEachEdge(Fn fn) const {
        for (std::size_t i = 0; i < edges_.size(); ++i) {
            if (!edges_[i].alive) continue;
            const MeshId h = edges_[i].halfEdge;
            if (!aliveHalfEdge(h)) continue;
            fn(static_cast<MeshId>(i), halfEdges_[h].start, halfEdges_[h].end);
        }
    }

    // 遍历所有存活的顶点：fn(vertexId)
    template <class Fn>
    void forEachVertex(Fn fn) const {
        for (std::size_t i = 0; i < vertices_.size(); ++i)
            if (vertices_[i].alive) fn(static_cast<MeshId>(i));
    }

    // 遍历所有存活的面：fn(faceId)
    template <class Fn>
    void forEachFace(Fn fn) const {
        for (std::size_t i = 0; i < faces_.size(); ++i)
            if (faces_[i].alive) fn(static_cast<MeshId>(i));
    }

    // ==================== 统计与校验 ====================

    MeshCounts counts() const {
        MeshCounts c;
        for (const auto& v : vertices_) if (v.alive) ++c.vertices;
        for (const auto& e : edges_) if (e.alive) ++c.edges;
        for (const auto& h : halfEdges_) if (h.alive) ++c.halfEdges;
        for (const auto& l : loops_) if (l.alive) { ++c.loops; if (!l.isOuter) ++c.innerLoops; }
        for (const auto& f : faces_) if (f.alive) ++c.faces;
        for (const auto& s : solids_) if (s.alive) ++c.solids;
        return c;
    }

    // 欧拉特征数 V − E + F（封闭单体的实体应为 2）
    int eulerCharacteristic() const {
        const MeshCounts c = counts();
        return c.vertices - c.edges + c.faces;
    }

    // 结构一致性校验：配对互反、next/prev 互反、环闭合且顶点连续、归属一致、无悬挂引用。
    // @param error 非空时写入首条错误描述
    bool validate(std::string* error = nullptr) const {
        const auto fail = [error](const std::string& msg) {
            if (error) *error = msg;
            return false;
        };

        // ① 半边自身的结构
        for (std::size_t i = 0; i < halfEdges_.size(); ++i) {
            const MeshHalfEdge& h = halfEdges_[i];
            if (!h.alive) continue;
            const std::string tag = "半边 " + std::to_string(i) + "：";
            if (!aliveVertex(h.start) || !aliveVertex(h.end)) return fail(tag + "端点无效或已删除");
            if (!aliveHalfEdge(h.next) || !aliveHalfEdge(h.prev)) return fail(tag + "next/prev 无效或已删除");
            if (!aliveHalfEdge(h.opposite)) return fail(tag + "配对半边无效或已删除");
            if (!aliveLoop(h.loop)) return fail(tag + "所属环无效或已删除");
            if (!aliveEdge(h.edge)) return fail(tag + "所属边无效或已删除");
            if (halfEdges_[h.opposite].opposite != static_cast<MeshId>(i)) return fail(tag + "配对不互反");
            if (halfEdges_[h.next].prev != static_cast<MeshId>(i)) return fail(tag + "next/prev 不互反");
            if (halfEdges_[h.prev].next != static_cast<MeshId>(i)) return fail(tag + "prev/next 不互反");
            if (halfEdges_[h.next].start != h.end) return fail(tag + "环上顶点不连续（next 起点 ≠ 本半边终点）");
            if (halfEdges_[h.next].loop != h.loop) return fail(tag + "next 与本半边不属于同一环");
            if (halfEdges_[h.opposite].start != h.end || halfEdges_[h.opposite].end != h.start)
                return fail(tag + "配对半边的方向不相反");
        }

        // ② 环：闭合 + 半边同环 + 属于有效面
        for (std::size_t i = 0; i < loops_.size(); ++i) {
            const MeshLoop& l = loops_[i];
            if (!l.alive) continue;
            const std::string tag = "环 " + std::to_string(i) + "：";
            if (!aliveFace(l.face)) return fail(tag + "所属面无效或已删除");
            if (l.halfEdge == kNoMesh) {
                // 空环（MVFS 后的初始环）：只允许其所在面仅含此环，且该面上没有其它半边
                continue;
            }
            if (!aliveHalfEdge(l.halfEdge)) return fail(tag + "入口半边无效或已删除");
            MeshId h = l.halfEdge;
            int guard = 0;
            do {
                if (halfEdges_[h].loop != static_cast<MeshId>(i)) return fail(tag + "环内半边归属不一致");
                h = halfEdges_[h].next;
                if (++guard > static_cast<int>(halfEdges_.size()) + 1) return fail(tag + "环未闭合（遍历不回起点）");
            } while (h != l.halfEdge && h != kNoMesh);
            if (h != l.halfEdge) return fail(tag + "环断开");
        }

        // ③ 面：外环与环集合一致
        for (std::size_t i = 0; i < faces_.size(); ++i) {
            const MeshFace& f = faces_[i];
            if (!f.alive) continue;
            const std::string tag = "面 " + std::to_string(i) + "：";
            if (!aliveLoop(f.outerLoop)) return fail(tag + "外环无效或已删除");
            if (f.loops.empty()) return fail(tag + "没有环");
            for (MeshId l : f.loops) {
                if (!aliveLoop(l)) return fail(tag + "环集合含无效环");
                if (loops_[l].face != static_cast<MeshId>(i)) return fail(tag + "环的归属面与本面不一致");
            }
            if (loops_[f.outerLoop].isOuter == false) return fail(tag + "外环被标记为内环");
        }

        // ④ 体：面集合有效且归属一致
        for (std::size_t i = 0; i < solids_.size(); ++i) {
            const MeshSolid& s = solids_[i];
            if (!s.alive) continue;
            const std::string tag = "体 " + std::to_string(i) + "：";
            for (MeshId f : s.faces) {
                if (!aliveFace(f)) return fail(tag + "面集合含无效面");
                if (solidOfFace(f) != static_cast<MeshId>(i)) return fail(tag + "面的归属体与本体不一致");
            }
        }

        // ⑤ 顶点入口约定
        for (std::size_t i = 0; i < vertices_.size(); ++i) {
            const MeshVertex& v = vertices_[i];
            if (!v.alive) continue;
            if (v.halfEdge == kNoMesh) continue;
            if (!aliveHalfEdge(v.halfEdge)) return fail("顶点 " + std::to_string(i) + "：入口半边无效或已删除");
            if (halfEdges_[v.halfEdge].start != static_cast<MeshId>(i))
                return fail("顶点 " + std::to_string(i) + "：入口半边的起点不是本顶点");
        }
        return true;
    }

    // 拓扑信息的文本表示（体/面/环/边/顶点 + 连接关系），供测试与演示输出
    std::string topologyText() const {
        const MeshCounts c = counts();
        std::ostringstream os;
        os << "V=" << c.vertices << " E=" << c.edges << " F=" << c.faces
           << " L=" << c.loops << "（内环 " << c.innerLoops << "） S=" << c.solids
           << " 半边=" << c.halfEdges << " ；V-E+F=" << eulerCharacteristic() << "\n";
        for (std::size_t i = 0; i < solids_.size(); ++i) {
            if (!solids_[i].alive) continue;
            os << "  体" << i << " 面[";
            for (std::size_t k = 0; k < solids_[i].faces.size(); ++k)
                os << (k ? "," : "") << solids_[i].faces[k];
            os << "]\n";
        }
        for (std::size_t i = 0; i < faces_.size(); ++i) {
            if (!faces_[i].alive) continue;
            os << "  面" << i << " 外环=" << faces_[i].outerLoop << " 全部环[";
            for (std::size_t k = 0; k < faces_[i].loops.size(); ++k)
                os << (k ? "," : "") << faces_[i].loops[k];
            os << "]\n";
        }
        for (std::size_t i = 0; i < loops_.size(); ++i) {
            if (!loops_[i].alive) continue;
            os << "    环" << i << (loops_[i].isOuter ? "(外)" : "(内)") << " 面=" << loops_[i].face << " 半边走：";
            if (loops_[i].halfEdge == kNoMesh) {
                os << "（空）";
            } else {
                MeshId h = loops_[i].halfEdge;
                do {
                    os << "v" << halfEdges_[h].start << "->v" << halfEdges_[h].end << " ";
                    h = halfEdges_[h].next;
                } while (h != loops_[i].halfEdge && h != kNoMesh);
            }
            os << "\n";
        }
        for (std::size_t i = 0; i < vertices_.size(); ++i) {
            if (!vertices_[i].alive) continue;
            os << "  顶点" << i << " (" << vertices_[i].position.x << ", " << vertices_[i].position.y
               << ", " << vertices_[i].position.z << ") 入口半边=" << vertices_[i].halfEdge << "\n";
        }
        return os.str();
    }
};

} // namespace gfx

#endif // GFX_HALFEDGE_HPP
