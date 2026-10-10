// bvh.hpp —— gfx 几何层：层次包围盒（BVH）加速结构
//
// @file 与**具体图元类型解耦**的 BVH：只依赖 Aabb 与 Ray（ray.hpp），不关心叶子背后是球、三角形还是别的。
//       构建：按包围盒中心的最长轴取中位数划分（std::nth_element）。查询：递归遍历 + 当前最优 t 剪枝，
//       对"可能与光线相交"的叶子里的每个图元调用回调做精确求交。
//
// 用途：光线跟踪的加速结构（把 O(N) 的求交降到接近 O(log N)）。
// 目标读者：使用 BVH 加速的调用方与教学读者（#include <gfx/geom/bvh.hpp>）。
// 分层语义：geom 层，**只依赖 math/**，不依赖 core/ 或 render/。
// 关联文档：docs/design/adr-0011-ray-geometry.md；gfx/geom/ray.hpp
//
// 状态：新增（2026-10-08）。BVH 加速（作业三沉淀为库能力）。
//
// 用法：
//   1) 用一组图元的 AABB 构建：`bvh.build(boxes);`
//   2) 查询最近命中：`bvh.closestHit(ray, visitor, t)`，回调签名 `bool(const Ray&, int index, float& outT)`
//      —— index 为图元下标（与 build 传入的 boxes 同序），命中则写 outT 并返回 true，否则返回 false。
//   3) 结果与"对所有图元线性遍历取最近"完全一致（见 tests/test_ray.cpp 的一致性用例）。

#ifndef GFX_GEOM_BVH_HPP
#define GFX_GEOM_BVH_HPP

#include "gfx/geom/ray.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

namespace gfx {

// 层次包围盒（BVH）：中位数划分 + 最近命中剪枝。
class Bvh {
private:
    // 节点：内部节点用 left/right 指向孩子；叶子用 start/count 描述图元区间（count > 0 即叶子）。
    struct Node {
        Aabb box;
        int  left  = -1;
        int  right = -1;
        int  start = 0;    // 叶子：order_ 中的起始下标
        int  count = 0;    // 叶子：图元个数；内部节点为 0
    };

    static constexpr int kLeafSize = 4;   // 叶子阈值（2–4 个图元）

    // 递归构建 [start, end) 区间的子树，返回其节点下标。
    int buildRecursive(const std::vector<Aabb>& boxes, int start, int end) {
        const int idx = static_cast<int>(nodes_.size());
        nodes_.push_back(Node{});
        nodes_[idx].box = boxes[order_[start]];
        for (int i = start + 1; i < end; ++i) nodes_[idx].box = aabbUnion(nodes_[idx].box, boxes[order_[i]]);

        const int count = end - start;
        if (count <= kLeafSize) {                 // 叶子
            nodes_[idx].start = start;
            nodes_[idx].count = count;
            ++leafCount_;
            return idx;
        }

        // 按包围盒**中心**在最**长轴**上的分布取中位数划分
        Aabb centerBox{nodes_[idx].box.center(), nodes_[idx].box.center()};
        for (int i = start; i < end; ++i) {
            const math::Vec3 c = boxes[order_[i]].center();
            centerBox = aabbUnion(centerBox, Aabb{c, c});
        }
        const math::Vec3 ext = centerBox.extent();
        int axis = 0;
        if (ext.y > ext[axis]) axis = 1;
        if (ext.z > ext[axis]) axis = 2;

        const int mid = start + count / 2;
        std::nth_element(order_.begin() + start, order_.begin() + mid, order_.begin() + end,
                         [&boxes, axis](int i, int j) {
                             return boxes[i].center()[axis] < boxes[j].center()[axis];
                         });
        const int l = buildRecursive(boxes, start, mid);
        const int r = buildRecursive(boxes, mid, end);
        nodes_[idx].left = l;
        nodes_[idx].right = r;
        return idx;
    }

    // 最近命中递归遍历：先近后远，按当前最优 t 剪枝。
    template <class Visitor>
    void closestHitRec(const Ray& ray, Visitor& visitor, int nodeIdx, float& bestT, bool& found) const {
        ++lastVisits_;
        const Node& node = nodes_[nodeIdx];
        float tNear = 0.0f, tFar = 0.0f;
        if (!intersectAabb(ray, node.box, tNear, tFar)) return;   // 节点盒未命中
        if (tNear > bestT) return;                                // 当前最优剪枝

        if (node.count > 0) {                                     // 叶子：逐个图元精确求交
            for (int i = 0; i < node.count; ++i) {
                float t = 0.0f;
                if (visitor(ray, order_[node.start + i], t) && t < bestT) {
                    bestT = t;
                    found = true;
                }
            }
            return;
        }

        // 内部节点：先访问更近的孩子，尽早收紧 bestT 以提高剪枝率
        float ltn = 0.0f, ltf = 0.0f, rtn = 0.0f, rtf = 0.0f;
        const bool lhit = intersectAabb(ray, nodes_[node.left].box, ltn, ltf);
        const bool rhit = intersectAabb(ray, nodes_[node.right].box, rtn, rtf);
        const bool leftFirst = lhit && (!rhit || ltn <= rtn);
        if (leftFirst) {
            if (lhit) closestHitRec(ray, visitor, node.left, bestT, found);
            if (rhit) closestHitRec(ray, visitor, node.right, bestT, found);
        } else {
            if (rhit) closestHitRec(ray, visitor, node.right, bestT, found);
            if (lhit) closestHitRec(ray, visitor, node.left, bestT, found);
        }
    }

    std::vector<Node> nodes_;        // 节点池（0 号为根；空树则为空）
    std::vector<int>  order_;        // 图元索引（build 时按划分重排）
    std::size_t       leafCount_ = 0;
    mutable std::size_t lastVisits_ = 0;   // 最近一次 closestHit 遍历过的节点数（可变更计数器）

public:
    // 原地构建：boxes 为各图元的 AABB（下标即回调收到的图元 index）。
    void build(const std::vector<Aabb>& boxes) {
        nodes_.clear();
        order_.clear();
        leafCount_ = 0;
        lastVisits_ = 0;
        if (boxes.empty()) return;

        const int n = static_cast<int>(boxes.size());
        order_.resize(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) order_[static_cast<std::size_t>(i)] = i;
        nodes_.reserve(static_cast<std::size_t>(n) * 2);
        buildRecursive(boxes, 0, n);
    }

    // 最近命中查询。
    // @param visitor 回调：bool(const Ray&, int index, float& outT)——命中则写 outT（其 t）并返回 true。
    // @param outT    命中时写入最近命中的 t
    // @return 是否任一次回调命中成功
    template <class Visitor>
    bool closestHit(const Ray& ray, Visitor&& visitor, float& outT) const {
        lastVisits_ = 0;
        if (nodes_.empty()) return false;
        float bestT = std::numeric_limits<float>::infinity();
        bool found = false;
        closestHitRec(ray, visitor, 0, bestT, found);
        if (found) outT = bestT;
        return found;
    }

    // 收集与光线**可能相交**的候选叶子节点下标（调试/统计用；不做精确求交、不做最近剪枝）。
    void candidates(const Ray& ray, std::vector<int>& outIndices) const {
        outIndices.clear();
        if (nodes_.empty()) return;
        std::vector<int> stack;
        stack.push_back(0);
        while (!stack.empty()) {
            const int idx = stack.back();
            stack.pop_back();
            float tNear = 0.0f, tFar = 0.0f;
            if (!intersectAabb(ray, nodes_[idx].box, tNear, tFar)) continue;
            if (nodes_[idx].count > 0) {
                outIndices.push_back(idx);
            } else {
                stack.push_back(nodes_[idx].left);
                stack.push_back(nodes_[idx].right);
            }
        }
    }

    std::size_t nodeCount() const { return nodes_.size(); }
    std::size_t leafCount() const { return leafCount_; }

    // 最近一次 closestHit 遍历过的节点数（剪枝效果的度量）。
    std::size_t lastVisitCount() const { return lastVisits_; }
};

} // namespace gfx

#endif // GFX_GEOM_BVH_HPP
