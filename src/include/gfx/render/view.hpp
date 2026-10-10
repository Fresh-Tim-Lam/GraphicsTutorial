// view.hpp —— gfx 相机 / 视图：世界坐标 ↔ 像素坐标映射（2D 正交 + 3D 预留）
//
// @file 提供"相机（View）"抽象：把**世界坐标**映射到**像素坐标**（及反向），集中管理
//       视口尺寸、观察中心与缩放；对应图形学里"视图变换 + 视口变换"两段的合并。
//       - 2D（当前主力）：正交视图 —— 世界矩形**等比**装入像素视口（contain），
//         含 y 轴翻转（世界 y 向上、像素 y 向下）、缩放、平移、居中；
//       - 3D：lookAt 观察矩阵 + 透视/正交投影矩阵，以及**投影到像素**的完整链路：
//         projectToPixel（单点）、projectSegment（线段，**含近平面裁剪**）、drawLine3D（投影 + 画线）。
//         仍无 3D 光栅管线（三角形填充 / 深度缓冲），故当前只支持**线框**绘制。
//
// @note 一致性：2D 的 worldToPixel() 与 matrix()/asTransform() **完全等价**（同一公式、
//       由测试逐点对照）；矩阵为行主序 Mat3（见 gfx/math/mat.hpp），作用方式 `M * Vec3(x, y, 1)`。
// @note 2D 只有**一个**缩放系数 ⇒ 等比缩放，不产生非等比拉伸。世界矩形与视口纵横比不一致时，
//       较松的方向留白（contain），由 fitWorld 保证"完整装入"；需要非等比映射请自行组合 Mat3。
// @note 坐标约定：世界坐标 y 向上（数学惯例），像素坐标 y 向下（Canvas 惯例），转换含翻转。
//
// 用途：绘制前把世界坐标几何映射到像素坐标（交给 raster / Canvas），或反向做拾取（pixelToWorld）。
// 目标读者：gfx 使用方与实现者（#include <gfx/render/view.hpp>）。
// 关联文档：docs/design/adr-0006-camera-view.md；gfx/core/canvas.hpp；gfx/math/mat.hpp；gfx/math/transform.hpp
//
// 状态：新增（2026-09-16）。

#ifndef GFX_VIEW_HPP
#define GFX_VIEW_HPP

#include "gfx/core/canvas.hpp"
#include "gfx/math/mat.hpp"
#include "gfx/math/transform.hpp"
#include "gfx/math/vec.hpp"
#include "gfx/render/raster.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace gfx {

namespace detail {

// 圆周率（仅本模块内部使用；不用 M_PI 以避开平台/宏差异）
inline constexpr float VIEW_PI = 3.14159265358979323846f;

} // namespace detail

// 投影类型
enum class ProjectionKind {
    Orthographic,   // 正交投影（setOrthographic）
    Perspective,    // 透视投影（setPerspective）
};

// 视体（view volume）参数：相机能看到的空间区域。
//   - 正交：就是长方体 [left, right] × [bottom, top]，深度范围 [nearZ, farZ]
//   - 透视：不是长方体而是**棱台（视锥）**，此处给出它在**近平面处**的矩形（l/r/b/t）与深度范围
// @note 深度用**正的距离**（nearZ = 近平面到相机的距离）；若按视空间 z 坐标写则 nearZ 对应 z = −nearZ
//       （讲义的 n、f 就是这种负 z，故讲义里 n > f）。
struct ViewFrustum {
    float left = 0.0f;      // 左边界（相机空间 x）
    float right = 0.0f;     // 右边界
    float bottom = 0.0f;    // 下边界（相机空间 y）
    float top = 0.0f;       // 上边界
    float nearZ = 0.0f;     // 近平面距离（> 0）
    float farZ = 0.0f;      // 远平面距离（> nearZ）

    // 近平面处的宽高（正交：视体宽高；透视：近平面矩形宽高）
    float width() const { return right - left; }
    float height() const { return top - bottom; }
};

// 相机 / 视图：世界坐标 ↔ 像素坐标的双向映射。
//
// 2D 映射公式（halfW = 视口宽/2，halfH = 视口高/2，s = 缩放，c = 世界观察中心）：
//   像素.x = halfW + (世界.x − c.x) · s
//   像素.y = halfH − (世界.y − c.y) · s        ← 减号即 y 轴翻转
// 逆映射：
//   世界.x = c.x + (像素.x − halfW) / s
//   世界.y = c.y − (像素.y − halfH) / s
//
// 典型用法：
//   gfx::View view;
//   view.setViewport(canvas);                       // 视口 = 画布尺寸
//   view.fitPoints(worldVertices, 24.0f);           // 顶点集等比装入，四边留 24px
//   const std::vector<gfx::math::Vec2> pixels = view.worldToPixel(worldVertices);
//   gfx::fillPolygon(canvas, pixels, color);        // 交给光栅层按像素坐标绘制
class View {
private:
    // 把 p 沿线段 q→p 裁到 w == nearPlaneW_ 处（前置：q.w ≥ nearPlaneW_ > p.w ⇒ 交点必在 q→p 之间）
    math::Vec4 clipToNearPlane(const math::Vec4& q, const math::Vec4& p) const {
        const float t = (nearPlaneW_ - q.w) / (p.w - q.w);
        return math::Vec4(q.x + (p.x - q.x) * t,
                          q.y + (p.y - q.y) * t,
                          q.z + (p.z - q.z) * t,
                          q.w + (p.w - q.w) * t);
    }

    // NDC 点 → 相机空间（投影矩阵求逆 + 齐次除法）；w == 0 时失败
    static bool unprojectNdc(const math::Mat4& invProj, float nx, float ny, float nz,
                             math::Vec3& outCam) {
        const math::Vec4 h = invProj * math::Vec4(nx, ny, nz, 1.0f);
        return math::fromHomogeneous(h, outCam);
    }

    // ---- 2D 正交视图状态 ----
    int viewportX_ = 0;                       // 视口原点 x（画布像素）
    int viewportY_ = 0;                       // 视口原点 y（画布像素）
    int viewportW_ = 0;                       // 像素视口宽
    int viewportH_ = 0;                       // 像素视口高
    math::Vec2 center_{0.0f, 0.0f};           // 视口中心对应的世界点
    float scale_ = 1.0f;                      // 1 世界单位 = scale_ 像素

    // ---- 3D 状态 ----
    math::Mat4 view_ = math::Mat4::identity();         // 观察矩阵
    math::Mat4 projection_ = math::Mat4::identity();   // 投影矩阵
    ProjectionKind kind_ = ProjectionKind::Orthographic;   // 投影类型（默认单位阵 = 不投影）
    ViewFrustum frustum_;                              // 视体参数（setOrthographic/setPerspective 时记录）
    float fovY_ = 0.0f;                                // 垂直视场角（弧度；仅透视）
    float aspect_ = 1.0f;                              // 纵横比（透视 = 参数；正交 = 视体宽高比）
    float nearPlaneW_ = 1e-4f;                         // 近平面裁剪阈值（= 透视的 zNear；正交下 w ≡ 1）

public:
    View() = default;

    // 以像素视口尺寸构造（等价于默认构造后 setViewport；非正值忽略）
    View(int viewportWidth, int viewportHeight) { setViewport(viewportWidth, viewportHeight); }

    // ==================== 2D 正交视图 ====================

    // 设置像素视口尺寸（通常等于 Canvas 尺寸）。
    // @param viewportWidth/viewportHeight 像素宽高；非正值时忽略本次设置（保持原视口）
    // @return *this（链式）
    View& setViewport(int viewportWidth, int viewportHeight) {
        if (viewportWidth <= 0 || viewportHeight <= 0) return *this;
        viewportW_ = viewportWidth;
        viewportH_ = viewportHeight;
        return *this;
    }

    // 以画布尺寸设置像素视口（便捷重载）
    View& setViewport(const Canvas& canvas) { return setViewport(canvas.width(), canvas.height()); }

    // 设置视口**原点**：视口左上角在画布中的像素位置（默认 (0, 0)）。
    // @note 用途是分屏/多视口：同一块画布上并排两个视口，各持一个 View 设不同原点即可。
    View& setViewportOrigin(int x, int y) { viewportX_ = x; viewportY_ = y; return *this; }

    int viewportX() const { return viewportX_; }
    int viewportY() const { return viewportY_; }

    int viewportWidth() const { return viewportW_; }
    int viewportHeight() const { return viewportH_; }

    // 设置视口中心所对准的**世界**坐标点（平移相机）
    View& setCenter(const math::Vec2& worldCenter) { center_ = worldCenter; return *this; }

    // 视口中心对应的世界坐标点
    const math::Vec2& center() const { return center_; }

    // 设置缩放：1 个世界单位对应多少像素（须 > 0）
    // @note 非正值忽略；缩放围绕视口中心（即 center() 所指的世界点）进行
    View& setScale(float pixelsPerUnit) {
        if (pixelsPerUnit > 0.0f) scale_ = pixelsPerUnit;
        return *this;
    }

    // 1 个世界单位对应的像素数
    float scale() const { return scale_; }

    // 相对当前缩放放大/缩小（等价于 setScale(scale() * factor)）
    View& zoomBy(float factor) {
        if (factor > 0.0f) scale_ *= factor;
        return *this;
    }

    // 让世界矩形 [minWorld, maxWorld] **完整装入**视口（contain、保持纵横比），
    // 并把视口中心对准矩形中心。
    // @param minWorld/maxWorld 世界矩形的左下角 / 右上角（y 向上）
    // @param marginPx 四边各留的像素留白（默认 0）
    // @note 矩形退化（宽或高为 0）时用另一维定缩放；两者皆为 0（一个点）时只对准中心、不改缩放。
    View& fitWorld(const math::Vec2& minWorld, const math::Vec2& maxWorld, float marginPx = 0.0f) {
        center_ = math::Vec2((minWorld.x + maxWorld.x) * 0.5f, (minWorld.y + maxWorld.y) * 0.5f);

        const float availW = static_cast<float>(viewportW_) - 2.0f * marginPx;
        const float availH = static_cast<float>(viewportH_) - 2.0f * marginPx;
        if (availW <= 0.0f || availH <= 0.0f) return *this;   // 留白吃掉视口：只改中心

        const float worldW = maxWorld.x - minWorld.x;
        const float worldH = maxWorld.y - minWorld.y;

        float s = 0.0f;
        if (worldW > 0.0f && worldH > 0.0f) s = std::min(availW / worldW, availH / worldH);
        else if (worldW > 0.0f)             s = availW / worldW;
        else if (worldH > 0.0f)             s = availH / worldH;

        if (s > 0.0f) scale_ = s;
        return *this;
    }

    // 让一组世界点**完整装入**视口（取包围盒后走 fitWorld）
    // @note 空点集不改动本视图
    View& fitPoints(const std::vector<math::Vec2>& points, float marginPx = 0.0f) {
        if (points.empty()) return *this;
        math::Vec2 mn = points.front();
        math::Vec2 mx = points.front();
        for (const math::Vec2& p : points) {
            mn.x = std::min(mn.x, p.x);
            mn.y = std::min(mn.y, p.y);
            mx.x = std::max(mx.x, p.x);
            mx.y = std::max(mx.y, p.y);
        }
        return fitWorld(mn, mx, marginPx);
    }

    // 世界坐标 → 像素坐标（结果含小数，落像素由调用方决定取整策略；见 raster.hpp 的采样规则）
    math::Vec2 worldToPixel(const math::Vec2& world) const {
        const float ox = static_cast<float>(viewportX_);
        const float oy = static_cast<float>(viewportY_);
        const float halfW = static_cast<float>(viewportW_) * 0.5f;
        const float halfH = static_cast<float>(viewportH_) * 0.5f;
        return math::Vec2(ox + halfW + (world.x - center_.x) * scale_,
                          oy + halfH - (world.y - center_.y) * scale_);
    }

    // 像素坐标 → 世界坐标（worldToPixel 的逆映射）
    math::Vec2 pixelToWorld(const math::Vec2& pixel) const {
        const float ox = static_cast<float>(viewportX_);
        const float oy = static_cast<float>(viewportY_);
        const float halfW = static_cast<float>(viewportW_) * 0.5f;
        const float halfH = static_cast<float>(viewportH_) * 0.5f;
        return math::Vec2(center_.x + (pixel.x - ox - halfW) / scale_,
                          center_.y - (pixel.y - oy - halfH) / scale_);
    }

    // 批量：世界坐标数组 → 像素坐标数组（逐点等价于 worldToPixel）
    std::vector<math::Vec2> worldToPixel(const std::vector<math::Vec2>& world) const {
        std::vector<math::Vec2> out;
        out.reserve(world.size());
        for (const math::Vec2& p : world) out.push_back(worldToPixel(p));
        return out;
    }

    // 批量：像素坐标数组 → 世界坐标数组（逐点等价于 pixelToWorld）
    std::vector<math::Vec2> pixelToWorld(const std::vector<math::Vec2>& pixels) const {
        std::vector<math::Vec2> out;
        out.reserve(pixels.size());
        for (const math::Vec2& p : pixels) out.push_back(pixelToWorld(p));
        return out;
    }

    // 世界 → 像素的齐次矩阵（行主序 Mat3）；与 worldToPixel() 等价：matrix() * Vec3(x, y, 1)
    math::Mat3 matrix() const {
        const float ox = static_cast<float>(viewportX_);
        const float oy = static_cast<float>(viewportY_);
        const float halfW = static_cast<float>(viewportW_) * 0.5f;
        const float halfH = static_cast<float>(viewportH_) * 0.5f;
        return math::Mat3{{scale_, 0.0f,    ox + halfW - center_.x * scale_},
                          {0.0f,  -scale_,  oy + halfH + center_.y * scale_},
                          {0.0f,   0.0f,    1.0f}};
    }

    // 像素 → 世界的齐次矩阵（matrix() 的逆）
    math::Mat3 inverseMatrix() const {
        const float ox = static_cast<float>(viewportX_);
        const float oy = static_cast<float>(viewportY_);
        const float halfW = static_cast<float>(viewportW_) * 0.5f;
        const float halfH = static_cast<float>(viewportH_) * 0.5f;
        const float inv = 1.0f / scale_;
        return math::Mat3{{inv,   0.0f,  center_.x - (ox + halfW) * inv},
                          {0.0f, -inv,   center_.y + (oy + halfH) * inv},
                          {0.0f,  0.0f,  1.0f}};
    }

    // 取出可复用的变换链（挂到 Point2D / PointBatch2D 上时等价于本视图的 worldToPixel）
    math::Transform<3> asTransform() const { return math::Transform<3>(matrix()); }

    // 当前可见范围的世界包围盒左下角（y 向上）
    math::Vec2 visibleWorldMin() const {
        const float halfW = static_cast<float>(viewportW_) * 0.5f;
        const float halfH = static_cast<float>(viewportH_) * 0.5f;
        return math::Vec2(center_.x - halfW / scale_, center_.y - halfH / scale_);
    }

    // 当前可见范围的世界包围盒右上角（y 向上）
    math::Vec2 visibleWorldMax() const {
        const float halfW = static_cast<float>(viewportW_) * 0.5f;
        const float halfH = static_cast<float>(viewportH_) * 0.5f;
        return math::Vec2(center_.x + halfW / scale_, center_.y + halfH / scale_);
    }

    // ==================== 3D：观察、投影与绘制 ====================

    // 构造右手系观察矩阵：相机位于 eye，看向 target，up 为上方向（世界→相机）。
    // @note eye == target（无视线方向）或 up 与视线平行（叉积退化）时**保持原矩阵不变**。
    View& lookAt(const math::Vec3& eye, const math::Vec3& target, const math::Vec3& up) {
        math::Vec3 f;
        if (!math::normalize(target - eye, f)) return *this;          // 后向轴（相机看向 −f）
        math::Vec3 s;
        if (!math::normalize(math::cross(f, up), s)) return *this;    // 右向轴
        const math::Vec3 u = math::cross(s, f);                       // 上向轴（已正交化）

        view_ = math::Mat4{{ s.x,  s.y,  s.z, -math::dot(s, eye)},
                           { u.x,  u.y,  u.z, -math::dot(u, eye)},
                           {-f.x, -f.y, -f.z,  math::dot(f, eye)},
                           {0.0f, 0.0f, 0.0f,  1.0f}};
        return *this;
    }

    // 透视投影（右手系，NDC z ∈ [−1, 1]，与 OpenGL 惯例一致）。
    // @param fovYRadians 垂直视场角（**弧度**，须 ∈ (0, π)）
    // @param aspect 宽高比（须 > 0，通常 = 视口宽 / 视口高）
    // @param zNear 近裁剪面距离（须 > 0）
    // @param zFar 远裁剪面距离（须 > zNear）
    // @note 参数非法时**保持原投影不变**
    View& setPerspective(float fovYRadians, float aspect, float zNear, float zFar) {
        if (!(fovYRadians > 0.0f) || !(fovYRadians < detail::VIEW_PI)) return *this;
        if (!(aspect > 0.0f) || !(zNear > 0.0f) || !(zFar > zNear)) return *this;

        const float f = 1.0f / std::tan(fovYRadians * 0.5f);
        projection_ = math::Mat4{
            {f / aspect, 0.0f, 0.0f, 0.0f},
            {0.0f,       f,    0.0f, 0.0f},
            {0.0f,       0.0f, (zFar + zNear) / (zNear - zFar), (2.0f * zFar * zNear) / (zNear - zFar)},
            {0.0f,       0.0f, -1.0f, 0.0f}};
        nearPlaneW_ = zNear;   // 线段裁剪的 w 阈值 = 近平面（其 NDC 深度为 −1）
        kind_ = ProjectionKind::Perspective;
        frustum_ = frustumOfPerspective(fovYRadians, aspect, zNear, zFar);
        fovY_ = fovYRadians;
        aspect_ = aspect;
        return *this;
    }

    // 正交投影（右手系，NDC z ∈ [−1, 1]）。
    // @param left/right 左右裁剪面（须 right > left）；@param bottom/top 上下裁剪面（须 top > bottom）
    // @param zNear/zFar 近远裁剪面（须 zFar > zNear）
    // @note 参数非法时**保持原投影不变**
    View& setOrthographic(float left, float right, float bottom, float top,
                          float zNear, float zFar) {
        if (!(right > left) || !(top > bottom) || !(zFar > zNear)) return *this;

        projection_ = math::Mat4{
            {2.0f / (right - left),   0.0f,                     0.0f,                     -(right + left) / (right - left)},
            {0.0f,                    2.0f / (top - bottom),    0.0f,                     -(top + bottom) / (top - bottom)},
            {0.0f,                    0.0f,                    -2.0f / (zFar - zNear),    -(zFar + zNear) / (zFar - zNear)},
            {0.0f,                    0.0f,                     0.0f,                      1.0f}};
        kind_ = ProjectionKind::Orthographic;
        frustum_ = ViewFrustum{left, right, bottom, top, zNear, zFar};
        fovY_ = 0.0f;
        aspect_ = (right - left) / (top - bottom);
        return *this;
    }

    // 观察矩阵（世界 → 相机；默认单位矩阵 = 相机在原点看向 −z）
    const math::Mat4& viewMatrix() const { return view_; }

    // 相机在世界空间的**视线方向**（单位矢量：从 eye 指向 target）。
    // @note 观察矩阵第 3 行存的是 −视线（lookAt 把 −f 写进去），取负即得 f；与投影无关。
    //       默认视图（单位矩阵）给出 (0, 0, −1)，即"看向 −z"，与 viewMatrix 的语义一致。
    // @note 用途：需要"光/相机方向"的世界空间矢量的调用方（如阴影贴图的光源方向）。
    math::Vec3 forward() const {
        return math::Vec3(-view_(2, 0), -view_(2, 1), -view_(2, 2));
    }

    // 投影矩阵（相机 → 裁剪空间；默认单位矩阵）
    const math::Mat4& projectionMatrix() const { return projection_; }

    // 组合矩阵：**投影 × 观察**（Mat 约定 A * B = 先作用 B ⇒ 先把世界点变到相机空间，再投影）
    math::Mat4 viewProjectionMatrix() const { return projection_ * view_; }

    // 裁剪空间 → 像素坐标（透视除法 + NDC → 视口 + 视口原点偏移 + y 翻转）。
    // @note 公开的理由：**需要齐次 w** 的调用方（光栅化着色管线的透视校正、阴影贴图的深度比较）
    //       必须先自己乘出裁剪空间点再映射，公开这一层比让各处复刻同一套映射公式更可靠。
    // @note 调用方需保证 clip.w > 0（相机后方/近平面外的点应先裁剪）。
    math::Vec2 clipToPixel(const math::Vec4& clip) const {
        const float invW = 1.0f / clip.w;
        const float ox = static_cast<float>(viewportX_);
        const float oy = static_cast<float>(viewportY_);
        return math::Vec2(ox + (clip.x * invW * 0.5f + 0.5f) * static_cast<float>(viewportW_),
                          oy + (1.0f - (clip.y * invW * 0.5f + 0.5f)) * static_cast<float>(viewportH_));
    }

    // 把世界点经 view × projection 投影到**像素坐标**（NDC → 视口，含 y 翻转）。
    // @note **不做裁剪**——单点无从裁起：点位于相机后方（齐次 w ≤ 0）时结果无定义（会镜像）。
    //       要画 3D 线段请用 projectSegment() / drawLine3D()，它们含近平面裁剪。
    math::Vec2 projectToPixel(const math::Vec3& world) const {
        return clipToPixel(viewProjectionMatrix() * math::Vec4(world.x, world.y, world.z, 1.0f));
    }

    // 把世界线段投影到**像素坐标**，含**近平面裁剪**。
    // @param outA/outB 投影后的像素坐标（仅当返回 true 时有效）
    // @return 线段有可见部分返回 true；整段都在相机后方返回 false（此时不应绘制）。
    // @note 为什么必须裁：透视投影下相机后方的点齐次 w < 0，直接做透视除法会把它**镜像翻到画面内**
    //       （表现为"鬼影线"）。故先在**裁剪空间**把端点裁到近平面（w = zNear，其 NDC 深度为 −1）再除法。
    // @note 正交投影下 w 恒为 1，裁剪自然不触发。
    bool projectSegment(const math::Vec3& a, const math::Vec3& b,
                        math::Vec2& outA, math::Vec2& outB) const {
        const math::Mat4 m = viewProjectionMatrix();
        math::Vec4 ca = m * math::Vec4(a.x, a.y, a.z, 1.0f);
        math::Vec4 cb = m * math::Vec4(b.x, b.y, b.z, 1.0f);

        const bool aVisible = (ca.w >= nearPlaneW_);
        const bool bVisible = (cb.w >= nearPlaneW_);
        if (!aVisible && !bVisible) return false;         // 整段在相机后方
        if (!aVisible) ca = clipToNearPlane(cb, ca);      // 把 a 端裁到近平面
        if (!bVisible) cb = clipToNearPlane(ca, cb);      // 把 b 端裁到近平面

        outA = clipToPixel(ca);
        outB = clipToPixel(cb);
        return true;
    }

    // 投影并绘制一条世界线段（= projectSegment + gfx::drawLine；坐标为平铺浮点，保留亚像素）
    void drawLine3D(Canvas& canvas, const math::Vec3& a, const math::Vec3& b,
                    const Color& color) const {
        math::Vec2 pa, pb;
        if (!projectSegment(a, b, pa, pb)) return;
        drawLine(canvas, pa.x, pa.y, pb.x, pb.y, color);
    }

    // ==================== 投影参数：查询与换算 ====================

    // 当前投影类型（默认 Orthographic，对应单位投影矩阵 = "不投影"）
    ProjectionKind projectionKind() const { return kind_; }

    // 当前视体参数（正交：完整长方体；透视：近平面处矩形 + 深度范围）
    const ViewFrustum& frustum() const { return frustum_; }

    // 垂直视场角（**弧度**；仅透视有意义，正交时为 0）
    float fovYRadians() const { return fovY_; }

    // 纵横比（透视 = 宽高比参数；正交 = 视体宽 / 视体高）
    float aspect() const { return aspect_; }

    // 由透视参数求视体的**近平面矩形**（透视图里 l/r/b/t 的来源）：
    //   top = nearZ·tan(fovY/2)、bottom = −top、right = top·aspect、left = −right
    // @note fovY 是"垂直"视场角，横向范围由 aspect 决定。讲义 p46 的 l/r/b/t 与 p51 的相似三角形
    //       描述的是同一件事：两种参数化可以互相换算。
    static ViewFrustum frustumOfPerspective(float fovYRadians, float aspect,
                                            float zNear, float zFar) {
        const float top = zNear * std::tan(fovYRadians * 0.5f);
        const float right = top * aspect;
        return ViewFrustum{-right, right, -top, top, zNear, zFar};
    }

    // 由视体的近平面矩形反求 fovY 与 aspect（frustumOfPerspective 的逆）
    // @return 视体退化（nearZ ≤ 0 或 top ≤ bottom）时返回 false，输出不变
    static bool perspectiveOfFrustum(const ViewFrustum& f, float& fovYRadians, float& aspect) {
        if (!(f.nearZ > 0.0f) || !(f.top > f.bottom)) return false;
        fovYRadians = 2.0f * std::atan(f.top / f.nearZ);
        aspect = (f.right - f.left) / (f.top - f.bottom);
        return true;
    }

    // ==================== 深度与投影分解 ====================

    // 投影到 **NDC**：out = (x_ndc, y_ndc, z_ndc) ∈ [−1, 1]³（可见范围内）。
    // @return 相机后方（齐次 w ≤ 0）时返回 false，out 不变
    // @note z_ndc 是深度测试/排序要用的量；本库当前只画线框，不做深度缓冲。
    bool projectToNdc(const math::Vec3& world, math::Vec3& out) const {
        const math::Vec4 clip = viewProjectionMatrix() * math::Vec4(world.x, world.y, world.z, 1.0f);
        if (!(clip.w > 0.0f)) return false;   // 相机后方（w ≤ 0）：除以它得到的坐标是镜像的，无意义
        return math::fromHomogeneous(clip, out);
    }

    // 透视投影的**两阶段分解**（讲义 p50-56）：`projectionMatrix() == ortho · perspToOrtho`
    struct ProjectionDecomposition {
        math::Mat4 perspToOrtho;   // 视锥压缩：把棱台压成"近平面大小的长方体"（产生近大远小）
        math::Mat4 ortho;          // 正交规范化：把该长方体映射到 [−1, 1]³
    };

    // @note 正交投影时 perspToOrtho = 单位阵、ortho = projectionMatrix()，分解恒成立（语义自洽）。
    ProjectionDecomposition projectionDecomposition() const {
        ProjectionDecomposition d;
        if (kind_ != ProjectionKind::Perspective) {
            d.perspToOrtho = math::Mat4::identity();
            d.ortho = projection_;
            return d;
        }
        const float n = frustum_.nearZ;
        const float f = frustum_.farZ;
        // 压缩：x' = n·x、y' = n·y、z' = (n+f)·z + n·f、w' = −z（z 是相机空间 z，负值）
        d.perspToOrtho = math::Mat4{{n,    0.0f, 0.0f,    0.0f},
                                    {0.0f, n,    0.0f,    0.0f},
                                    {0.0f, 0.0f, n + f,   n * f},
                                    {0.0f, 0.0f, -1.0f,   0.0f}};
        // 规范化：把 [l,r] × [b,t] × [−farZ,−nearZ] 映射到 [−1,1]³（与 setOrthographic(近平面矩形, n, f) 同矩阵）
        const float l = frustum_.left, r = frustum_.right;
        const float b = frustum_.bottom, t = frustum_.top;
        d.ortho = math::Mat4{{2.0f / (r - l), 0.0f,            0.0f,             -(r + l) / (r - l)},
                             {0.0f,            2.0f / (t - b), 0.0f,             -(t + b) / (t - b)},
                             {0.0f,            0.0f,           2.0f / (n - f),   (f + n) / (n - f)},
                             {0.0f,            0.0f,           0.0f,             1.0f}};
        return d;
    }

    // ==================== 相机基、位置与反投影 ====================

    // 相机的三根基向量（世界坐标，单位向量）；相机看向 −Z，故 cameraBack() = −观察方向
    math::Vec3 cameraRight() const { return math::Vec3(view_(0, 0), view_(0, 1), view_(0, 2)); }
    math::Vec3 cameraUp() const    { return math::Vec3(view_(1, 0), view_(1, 1), view_(1, 2)); }
    math::Vec3 cameraBack() const  { return math::Vec3(view_(2, 0), view_(2, 1), view_(2, 2)); }

    // 相机位置（世界坐标）：V = [R | −R·e] 且 R 正交 ⇒ e = −Rᵀ·t
    math::Vec3 eyePosition() const {
        const float tx = view_(0, 3), ty = view_(1, 3), tz = view_(2, 3);
        return math::Vec3(-(view_(0, 0) * tx + view_(1, 0) * ty + view_(2, 0) * tz),
                          -(view_(0, 1) * tx + view_(1, 1) * ty + view_(2, 1) * tz),
                          -(view_(0, 2) * tx + view_(1, 2) * ty + view_(2, 2) * tz));
    }

    // **相机 → 世界**矩阵 C = V⁻¹（讲义 p37-38）：等价于 frame(右轴, 上轴, 后轴, 相机位置)
    // @note 反投影/推导都用它：C 的第 4 列就是相机位置。
    math::Mat4 cameraToWorldMatrix() const {
        return math::transform3d::frame(cameraRight(), cameraUp(), cameraBack(), eyePosition());
    }

    // ModelView 矩阵 V·M：把**模型空间**直接送到**相机空间**（讲义 p39）
    math::Mat4 modelViewMatrix(const math::Mat4& model) const { return view_ * model; }

    // 屏幕像素 → **世界射线**（反投影，用于 3D 拾取）。
    // @param outOrigin 射线起点 = 相机位置；@param outDirection 单位方向向量
    // @return 视口无效或投影/观察矩阵不可逆时返回 false
    bool pixelToRay(float pixelX, float pixelY, math::Vec3& outOrigin, math::Vec3& outDirection) const {
        if (viewportW_ <= 0 || viewportH_ <= 0) return false;
        math::Mat4 invProj;
        if (!projection_.inverse(invProj)) return false;
        math::Mat4 invView;
        if (!view_.inverse(invView)) return false;

        const float ndcX = (pixelX - static_cast<float>(viewportX_))
                         / static_cast<float>(viewportW_) * 2.0f - 1.0f;
        const float ndcY = 1.0f - (pixelY - static_cast<float>(viewportY_))
                         / static_cast<float>(viewportH_) * 2.0f;

        math::Vec3 nearCam, farCam;
        if (!unprojectNdc(invProj, ndcX, ndcY, -1.0f, nearCam)) return false;
        if (!unprojectNdc(invProj, ndcX, ndcY,  1.0f, farCam)) return false;

        const math::Vec4 o  = invView * math::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
        const math::Vec4 pn = invView * math::Vec4(nearCam.x, nearCam.y, nearCam.z, 1.0f);
        const math::Vec4 pf = invView * math::Vec4(farCam.x, farCam.y, farCam.z, 1.0f);
        outOrigin = math::Vec3(o.x, o.y, o.z);
        return math::normalize(math::Vec3(pf.x - pn.x, pf.y - pn.y, pf.z - pn.z), outDirection);
    }
};

} // namespace gfx

#endif // GFX_VIEW_HPP
