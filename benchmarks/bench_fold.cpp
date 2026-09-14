// bench_fold.cpp —— 变换链折叠路径的开销拆解（正式基准，可复现）
//
// 用途：回答"链的每一步到底贵在哪"，把 Transform<N> 折叠路径的开销分成三层量出来：
//       ① 折叠内核：identity 起手 / 首步赋值 / 免别名内核 的差异（矩阵搬运开销）；
//       ② 进链成本：push_back（无/有 reserve）对比"不进链"（立即左乘）；
//       ③ 端到端：k 步 then + 首消费 matrix() 的总开销。
//       为 docs/conventions/performance.md（P3 结论可复现）与 ADR-0004 的折叠路径决策提供依据。
//
// 构建/运行：
//   g++ -O2 -std=c++17 -I src/include benchmarks/bench_fold.cpp -o benchmarks/bench_fold.exe
//   benchmarks\bench_fold.exe
//
// 测量条件：GCC 14.2 / MinGW-w64 / -O2 / k = 3、8 步 / 每轮 50000 次 / 取 7 轮最小值。
// 基线（2026-09-14，单位 ns；端到端与每步成本两次连续运行复现一致，波动 < 5%）：
//
//   端到端 = k 步 then + 首次 matrix()        N=3 k=3   N=3 k=8   N=4 k=3   N=4 k=8
//   G 现状（生产 Transform<N>）                 101       181        92       151
//   H foldC（首步赋值+免别名内核）+ reserve      48       102        36        74
//   J foldB（首步赋值）+ reserve                 47       102        39        85
//   I 不进链（每步立即左乘，无链）               31        82        29        71
//   K 内联固定容量（完全无堆）+ 首步赋值         33        98       26        74
//   L 二分（平衡树）折叠 + reserve                48       113        38        76
//
//   每步成本（D 未预留 / E 已预留 / F 立即左乘）：k=3 → 23.1 / 10.5 / 10.8（N=3）、23.6 / 10.0 / 9.1（N=4）
//                                                 k=8 → 12.0 / 4.9 / 10.7（N=3）、12.3 / 4.9 / 9.2（N=4）
//
//   就地生成 vs 先建后拷（Mat3 同布局的 9 个 float；缓冲区复用，即分配已剔除；每步 ns）：
//   X1 成品矩阵 + push_back（现状形态）  7.25~7.32
//   X2 描述符 + emplace_back（就地构造）  1.06~1.14
//   X3 描述符 + 内联槽位（无堆、无拷贝）  0.88~0.95
//   ⇒ "先构造临时再搬进槽位"比"就地生成"贵 ~6.2 ns/步（7.3 vs 1.1，约 6.7x）；
//     容器间接寻址（X2 vs X3）仅 ~0.15 ns/步 ⇒ **拷贝是大头，分配在复用后可忽略**。
//
//   折叠内核 A(identity 起手) / B(首步赋值) / C(免别名内核) / M(二分平衡树折叠)（同一轮内比较有效；
//   绝对值受代码布局影响，跨运行波动可达 ±20%）：k=3 → A 27~43  B 13~29  C 12~33  M 13~22；
//   k=8 → A 66~111  B 40~99  C 40~116  M 48~89。规律：B、C 不慢于 A（N=4 明显更快）；M 在 k=3 与 C 持平，
//   在 k=8 反而慢 9~11 ns（N=3/N=4）—— 二分只降依赖深度、不减少乘法次数，而 N=3/4 的乘法太小填不满流水线，
//   并行度收益被临时量与递归开销吃掉；且二分改变结合顺序 ⇒ **不再逐位一致**（maxΔ 达 3.6e-07 / 9.5e-07，k=3 时为 0）。
//
// 结论：
//   ① **堆分配是链的最大单项开销**：只把 push_back 的容量预留（D→E），k=8 每步 12.0 → 4.9 ns
//      （整链 ~96 → ~39 ns）、k=3 每步 23.1 → 10.5 ns（整链 ~69 → ~32 ns）。生产 Transform<N>
//      未预留容量，一条 3 步链要经历 1→2→4 三次 malloc/free。
//   ② 端到端收益（G→H/J）：k=3 约 −50%~−60%，k=8 约 −45%~−50%；其中大头来自 ①（减少堆分配），
//      折叠内核改造只占几 ns~十几 ns。
//   ③ "消除折叠里 2 次多余 N² 拷贝"（C 免别名内核）**不是**通用收益：N=4 上有效，N=3 上落在噪声内，
//      端到端 H 与 J 的差距（N=3 k=8：102 vs 102；N=4 k=8：74 vs 85）说明它不值钱 ⇒ 不必为它引入
//      第二套（且形状相关的）乘法内核；"首步赋值"这一项便宜且全程不劣，可无风险采纳。
//   ④ "不进链、只要矩阵"（I）与最优链（K/H）同量级甚至更快 ⇒ 链当前的价值来自"折叠一次 + 缓存"，
//      "保留步骤"只贡献成本（尚无链级合并/重排/后端下发的实现）。对比反模式 F（每步重建矩阵）可见其代价。
//   ⑤ K（内联固定容量）仅在短链（k=3）占优（N=3 33 vs 48、N=4 26 vs 36），长链与 H 持平，
//      但每个 Transform 实例都要背 8×N² 字节（Point2D/PointBatch2D 内嵌 Transform ⇒ 对象体积膨胀），
//      收益与代价不匹配 ⇒ 暂不采纳，仅作"零堆"上限参照。
//
// 字段说明：A/B/C 折叠内核；D/E/F 每步成本；G/H/I/J/K 端到端"建链 + 首消费 matrix()"。
//
// 约定：每轮测量前把 pool[1](0,0) 从 volatile 读入，使折叠结果依赖运行期数据，
//       防止优化器把被测计算提升到循环外（否则量到的是"被优化掉"的速度）。

#include <gfx/math/math.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <new>
#include <vector>

using namespace gfx::math;

volatile float g_sink = 0.0f;
volatile float g_seed = 1.0f;   // volatile 读取不可提升 ⇒ 打断循环不变性

// ============ 三个折叠实现（与 transform.hpp 的 Transform<N>::fold 对应） ============

// A：现状 —— identity 起手，每步做一次完整 N³ 乘法（首步 s*I 是白算的）
template <int N>
inline Mat<N> foldA(const std::vector<Mat<N>>& steps) {
    Mat<N> acc = Mat<N>::identity();
    for (const Mat<N>& s : steps) acc = s * acc;
    return acc;
}

// B：首步直接赋值，省掉那次 s*I（其余仍是 Mat::operator* 路径）
template <int N>
inline Mat<N> foldB(const std::vector<Mat<N>>& steps) {
    if (steps.empty()) return Mat<N>::identity();
    Mat<N> acc = steps[0];
    for (std::size_t i = 1; i < steps.size(); ++i) acc = steps[i] * acc;
    return acc;
}

// 免别名乘法内核：三层循环直接写进新局部。
// 与 Mat::operator* 的差别：① 两入参均为 const& ⇒ 无按值拷贝；
// ② 目标是新对象 ⇒ 不需要 operator*= 里那句防别名的 `const Mat self = *this;`。
// （折叠路径里 acc 是局部、s 来自 vector，二者不可能别名，该快照纯属多余。）
template <int N>
inline Mat<N> mulTo(const Mat<N>& a, const Mat<N>& b) {
    Mat<N> r;
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j) {
            float acc = 0.0f;
            for (int k = 0; k < N; ++k) acc += a(i, k) * b(k, j);
            r(i, j) = acc;
        }
    return r;
}

// C：首步赋值 + 免别名内核
template <int N>
inline Mat<N> foldC(const std::vector<Mat<N>>& steps) {
    if (steps.empty()) return Mat<N>::identity();
    Mat<N> acc = steps[0];
    for (std::size_t i = 1; i < steps.size(); ++i) acc = mulTo(steps[i], acc);
    return acc;
}

// 二分（平衡树）折叠的递归体：返回 steps[hi-1] * ... * steps[lo]
// 每层把区间对半，左右各自成积后再合并 ⇒ 依赖深度 ⌈log2 k⌉（乘法总次数仍是 k-1）。
template <int N>
inline Mat<N> foldRange(const std::vector<Mat<N>>& s, std::size_t lo, std::size_t hi) {
    if (hi - lo == 1) return s[lo];
    const std::size_t mid = lo + (hi - lo) / 2;
    const Mat<N> left = foldRange<N>(s, lo, mid);    // 先作用的半边
    const Mat<N> right = foldRange<N>(s, mid, hi);   // 后作用的半边
    return right * left;                             // 左乘口径：后作用者在左
}

// M：二分（平衡树）折叠
template <int N>
inline Mat<N> foldM(const std::vector<Mat<N>>& steps) {
    if (steps.empty()) return Mat<N>::identity();
    return foldRange<N>(steps, 0, steps.size());
}

// ============ 对照用的"链"实现（只改折叠路径，其余语义与 Transform<N> 一致） ============

// H：生产 Transform<N> 的改进版 —— reserve + foldC
template <int N>
class FastTransform {
public:
    FastTransform() { steps_.reserve(8); }
    FastTransform& then(const Mat<N>& s) { steps_.push_back(s); valid_ = false; return *this; }
    const Mat<N>& matrix() const {
        if (!valid_) { cached_ = foldC(steps_); valid_ = true; }
        return cached_;
    }
private:
    std::vector<Mat<N>> steps_;
    mutable Mat<N> cached_;
    mutable bool valid_ = false;
};

// J：只做"确定收益"的改动 —— reserve + 首步赋值（保留 Mat::operator*，不换内核）
template <int N>
class ReserveFoldB {
public:
    ReserveFoldB() { steps_.reserve(8); }
    ReserveFoldB& then(const Mat<N>& s) { steps_.push_back(s); valid_ = false; return *this; }
    const Mat<N>& matrix() const {
        if (!valid_) { cached_ = foldB(steps_); valid_ = true; }
        return cached_;
    }
private:
    std::vector<Mat<N>> steps_;
    mutable Mat<N> cached_;
    mutable bool valid_ = false;
};

// L：reserve + 二分折叠
template <int N>
class BisectTransform {
public:
    BisectTransform() { steps_.reserve(8); }
    BisectTransform& then(const Mat<N>& s) { steps_.push_back(s); valid_ = false; return *this; }
    const Mat<N>& matrix() const {
        if (!valid_) { cached_ = foldM(steps_); valid_ = true; }
        return cached_;
    }
private:
    std::vector<Mat<N>> steps_;
    mutable Mat<N> cached_;
    mutable bool valid_ = false;
};

// I：不进链 —— 每步立即左乘（"矩阵"做法，无独立性可言）
template <int N>
class EagerTransform {
public:
    EagerTransform& then(const Mat<N>& s) { acc_ = mulTo(s, acc_); return *this; }
    const Mat<N>& matrix() const { return acc_; }
private:
    Mat<N> acc_ = Mat<N>::identity();
};

// K：内联固定容量（完全无堆）—— 用于量出"堆分配"这一项的上限参照（链长 <= 8）
template <int N>
class InlineTransform {
public:
    InlineTransform& then(const Mat<N>& s) {
        if (n_ < kCap) steps_[n_++] = s;
        valid_ = false;
        return *this;
    }
    const Mat<N>& matrix() const {
        if (!valid_) { cached_ = fold(); valid_ = true; }
        return cached_;
    }
private:
    static constexpr int kCap = 8;
    Mat<N> steps_[kCap];
    int n_ = 0;
    mutable Mat<N> cached_;
    mutable bool valid_ = false;

    Mat<N> fold() const {
        if (n_ == 0) return Mat<N>::identity();
        Mat<N> acc = steps_[0];
        for (int i = 1; i < n_; ++i) acc = steps_[i] * acc;
        return acc;
    }
};

// ============ 计时：同一被测体跑 reps 次，取 7 轮最小值 ============

template <typename Body>
double timeOnce(int reps, Body body) {
    double best = 1e300;
    for (int round = 0; round < 7; ++round) {
        const auto t0 = std::chrono::steady_clock::now();
        float chk = 0.0f;
        for (int i = 0; i < reps; ++i) chk += body();
        const auto t1 = std::chrono::steady_clock::now();
        g_sink = chk;
        best = std::min(best, std::chrono::duration<double, std::nano>(t1 - t0).count() / reps);
    }
    return best;
}

template <int N>
float maxAbsDiff(const Mat<N>& a, const Mat<N>& b) {
    float d = 0.0f;
    for (int i = 0; i < N * N; ++i) d = std::max(d, std::fabs(a.data[i] - b.data[i]));
    return d;
}

// 用同一条链跑某个链实现，并与参照矩阵比对（等价性校验）
template <template <int> class Chain, int N>
float chainDiff(const std::vector<Mat<N>>& steps, const Mat<N>& ref) {
    Chain<N> t;
    for (const Mat<N>& s : steps) t.then(s);
    return maxAbsDiff(ref, t.matrix());
}

// ============ 就地生成 vs 先建后拷 ============
//
// 问题：能否让矩阵"一开始就生成在链的槽位上"，从而消掉进链时那次 N² 拷贝？
// 做法：用与 Mat3 同布局（9 个 float）的本地类型隔离这一项，对比三种形态：
//   X1 现状形态：工厂返回**成品矩阵** → push_back（先构造临时，再拷进槽位）
//   X2 描述符形态：工厂返回**描述**（Tag）→ emplace_back 就地构造（只写一次）
//   X3 X2 + 内联槽位（无堆；placement new 直接构造在预分配的连续位置上）

struct TranslateTag { float x, y; };

struct MatLike3 {
    float data[9] = {};
    MatLike3() = default;
    explicit MatLike3(const TranslateTag& t) {   // 就地构造：直接写进目标 9 个 float
        data[0] = 1.0f; data[1] = 0.0f; data[2] = t.x;
        data[3] = 0.0f; data[4] = 1.0f; data[5] = t.y;
        data[6] = 0.0f; data[7] = 0.0f; data[8] = 1.0f;
    }
};

// 现状形态的工厂：返回成品矩阵（临时对象）
inline MatLike3 makeTranslate(float x, float y) {
    MatLike3 m;
    m.data[0] = 1.0f; m.data[1] = 0.0f; m.data[2] = x;
    m.data[3] = 0.0f; m.data[4] = 1.0f; m.data[5] = y;
    m.data[6] = 0.0f; m.data[7] = 0.0f; m.data[8] = 1.0f;
    return m;
}

static void runCopyOverhead() {
    const int reps = 50000;
    std::printf("== 就地生成 vs 先建后拷（每步 ns；Mat3 同布局；缓冲区复用，分配已剔除） ==\n");

    std::vector<MatLike3> buf;          // 复用的连续缓冲区（分配不计入本项）
    buf.reserve(8);

    for (int k : {3, 8}) {
        // X1 工厂返回成品 → push_back（构造 + 拷贝）
        const double x1 = timeOnce(reps, [&] {
            const float s = g_seed;
            buf.clear();
            for (int j = 0; j < k; ++j) buf.push_back(makeTranslate(s + j, s));
            float acc = 0.0f;
            for (const MatLike3& e : buf) acc += e.data[2];   // 读回全部 ⇒ 写入不可被消除
            return acc;
        }) / k;

        // X2 描述符 → emplace_back 就地构造（只写一次）
        const double x2 = timeOnce(reps, [&] {
            const float s = g_seed;
            buf.clear();
            for (int j = 0; j < k; ++j) buf.emplace_back(TranslateTag{s + j, s});
            float acc = 0.0f;
            for (const MatLike3& e : buf) acc += e.data[2];
            return acc;
        }) / k;

        // X3 描述符 → 内联槽位 placement new（无堆、无拷贝）
        const double x3 = timeOnce(reps, [&] {
            const float s = g_seed;
            alignas(16) unsigned char mem[sizeof(MatLike3) * 8];
            MatLike3* p = reinterpret_cast<MatLike3*>(mem);
            for (int j = 0; j < k; ++j) new (static_cast<void*>(p + j)) MatLike3(TranslateTag{s + j, s});
            float acc = 0.0f;
            for (int j = 0; j < k; ++j) acc += p[j].data[2];
            return acc;
        }) / k;

        std::printf("k = %d 步   X1 成品+push_back %7.2f   X2 描述+emplace %7.2f   X3 描述+内联槽位 %7.2f\n",
                    k, x1, x2, x3);
    }
    std::printf("\n");
}

// ============ 单个维度 N 的全部测量 ============

template <int N>
void run(const char* title, const std::vector<Mat<N>>& all) {
    const int reps = 50000;
    std::printf("== %s ==\n", title);

    for (int k : {3, 8}) {
        std::vector<Mat<N>> pool(all.begin(), all.begin() + k);

        // ---- ① 折叠内核：每次折叠（k 步） ----
        const double a = timeOnce(reps, [&] { pool[1](0, 0) = g_seed; return foldA(pool)(0, 0); });
        const double b = timeOnce(reps, [&] { pool[1](0, 0) = g_seed; return foldB(pool)(0, 0); });
        const double c = timeOnce(reps, [&] { pool[1](0, 0) = g_seed; return foldC(pool)(0, 0); });
        const double m = timeOnce(reps, [&] { pool[1](0, 0) = g_seed; return foldM(pool)(0, 0); });

        // ---- ② 进链/不进链：每步 ----
        // D：push_back，无 reserve（含扩容搬运）
        const double dTot = timeOnce(reps, [&] {
            pool[1](0, 0) = g_seed;
            std::vector<Mat<N>> v;
            for (int j = 0; j < k; ++j) v.push_back(pool[j]);
            return static_cast<float>(v.size());
        });
        // E：push_back，容量已预留（只剩 N² 拷贝）
        const double eTot = timeOnce(reps, [&] {
            pool[1](0, 0) = g_seed;
            std::vector<Mat<N>> v;
            v.reserve(k);
            for (int j = 0; j < k; ++j) v.push_back(pool[j]);
            return static_cast<float>(v.size());
        });
        // F：不进链 —— 立即左乘（每步 N³）
        const double fTot = timeOnce(reps, [&] {
            pool[1](0, 0) = g_seed;
            Mat<N> acc = Mat<N>::identity();
            for (int j = 0; j < k; ++j) acc = mulTo(pool[j], acc);
            return acc(0, 0);
        });

        // ---- ③ 端到端：k 步 then + 首消费 matrix() ----
        const double g = timeOnce(reps, [&] {
            pool[1](0, 0) = g_seed;
            Transform<N> t;
            for (int j = 0; j < k; ++j) t.then(pool[j]);
            return t.matrix()(0, 0);
        });
        const double h = timeOnce(reps, [&] {
            pool[1](0, 0) = g_seed;
            FastTransform<N> t;
            for (int j = 0; j < k; ++j) t.then(pool[j]);
            return t.matrix()(0, 0);
        });
        const double i = timeOnce(reps, [&] {
            pool[1](0, 0) = g_seed;
            EagerTransform<N> t;
            for (int j = 0; j < k; ++j) t.then(pool[j]);
            return t.matrix()(0, 0);
        });
        const double j = timeOnce(reps, [&] {
            pool[1](0, 0) = g_seed;
            ReserveFoldB<N> t;
            for (int j = 0; j < k; ++j) t.then(pool[j]);
            return t.matrix()(0, 0);
        });
        const double kk = timeOnce(reps, [&] {
            pool[1](0, 0) = g_seed;
            InlineTransform<N> t;
            for (int j = 0; j < k; ++j) t.then(pool[j]);
            return t.matrix()(0, 0);
        });
        const double l = timeOnce(reps, [&] {
            pool[1](0, 0) = g_seed;
            BisectTransform<N> t;
            for (int j = 0; j < k; ++j) t.then(pool[j]);
            return t.matrix()(0, 0);
        });

        // ---- 数值一致性（折叠内核 A/B/C/M、链实现 G/H/I/J/K/L 都必须与 A 等价） ----
        const Mat<N> ma = foldA(pool), mb = foldB(pool), mc = foldC(pool), mm = foldM(pool);
        const float dG = chainDiff<Transform, N>(pool, ma);
        const float dH = chainDiff<FastTransform, N>(pool, ma);
        const float dI = chainDiff<EagerTransform, N>(pool, ma);
        const float dJ = chainDiff<ReserveFoldB, N>(pool, ma);
        const float dK = chainDiff<InlineTransform, N>(pool, ma);
        const float dL = chainDiff<BisectTransform, N>(pool, ma);

        std::printf("k = %d 步\n", k);
        std::printf("  折叠内核   A(identity 起手) %7.2f   B(首步赋值) %7.2f   C(免别名内核) %7.2f   M(二分折叠) %7.2f  ns/次\n",
                    a, b, c, m);
        std::printf("  每步成本   D(push_back)     %7.2f   E(push_back+reserve) %7.2f   F(立即左乘) %7.2f  ns/步\n",
                    dTot / k, eTot / k, fTot / k);
        std::printf("  端到端     G(生产) %7.2f  H(C+res) %7.2f  I(不进链) %7.2f  J(B+res) %7.2f  K(内联) %7.2f  L(二分+res) %7.2f  ns\n",
                    g, h, i, j, kk, l);
        std::printf("  一致性     maxΔ(A,B)=%.3g  maxΔ(A,C)=%.3g  maxΔ(A,M)=%.3g  maxΔ(G,H,I,J,K,L vs A)=%.3g/%.3g/%.3g/%.3g/%.3g/%.3g\n\n",
                    maxAbsDiff(ma, mb), maxAbsDiff(ma, mc), maxAbsDiff(ma, mm), dG, dH, dI, dJ, dK, dL);
    }
}

int main() {
    std::printf("变换链折叠路径开销拆解（-O2；每轮 50000 次取 7 轮最小值；防提升见文件头）\n\n");

    // 2D 步骤池（8 步，覆盖平移/旋转/绕点旋转/缩放/翻转）
    const std::vector<Mat3> pool3{
        transform2d::translate(Vec2(5.0f, -3.0f)),
        transform2d::rotate(0.7f, Vec2(10.0f, 20.0f)),
        transform2d::scale(Vec2(2.0f, 3.0f)),
        transform2d::flipX(),
        transform2d::translate(Vec2(-1.0f, 4.0f)),
        transform2d::rotate(0.3f),
        transform2d::scale(1.5f),
        transform2d::flipY()};

    // 3D 步骤池
    const std::vector<Mat4> pool4{
        transform3d::translate(Vec3(5.0f, -3.0f, 1.0f)),
        transform3d::rotate(0.7f, Axis::Y),
        transform3d::scale(Vec3(2.0f, 3.0f, 0.5f)),
        transform3d::flipZ(),
        transform3d::rotate(0.3f, Vec3(1.0f, 2.0f, 3.0f), Vec3(4.0f, 5.0f, 6.0f)),
        transform3d::translate(Vec3(-1.0f, 4.0f, 2.0f)),
        transform3d::rotate(0.2f, Axis::X),
        transform3d::flipY()};

    run<3>("N = 3（Mat3）", pool3);
    run<4>("N = 4（Mat4）", pool4);
    runCopyOverhead();

    return 0;
}
