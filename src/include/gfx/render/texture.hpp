// texture.hpp —— gfx 纹理：内存里的 RGB 贴图 + 归一化 uv 采样（环绕 / 滤波）
//
// 用途：给"贴图类"效果提供最小基础设施。目前服务于法线贴图
//       （见 gfx/render/normalmap.hpp），也是将来接光栅化着色管线时的同一份纹理类型。
// 目标读者：需要按 uv 取色的调用方（#include <gfx/render/texture.hpp>）。
// 分层语义：render 层，只依赖 math/。**与渲染路径无关**：光追和光栅化都能用同一份 Texture。
//
// @note 像素一律以 **0..1 的线性 float 三元组**存储（若从 8bit 载入则除以 255），
//       与材质 / 光照的数值口径一致，免得采样点处反复做 8bit 转换。
// @note uv 约定：u 沿 +X（宽度方向）、v 沿 +Y（高度方向），纹理坐标以**像素中心**为 (i+0.5)/n。
//       环绕与滤波分别由 WrapMode / TextureFilter 控制，默认 Repeat + Bilinear。
//
// 关联文档：docs/design/adr-0013-normal-mapping.md；gfx/render/normalmap.hpp；gfx/math/vec.hpp
//
// 状态：新增（2026-10-09）。法线贴图沉淀为库能力。

#ifndef GFX_TEXTURE_HPP
#define GFX_TEXTURE_HPP

#include "gfx/math/math.hpp"

#include <cmath>
#include <vector>

namespace gfx {

// uv 超出 [0,1] 时的取法
enum class WrapMode {
    Repeat,   // 平铺（默认）：uv 取小数部分
    Clamp,    // 夹取：贴图边缘像素向外延伸
    Mirror    // 镜像平铺：每跨一格翻转一次
};

// 采样滤波
enum class TextureFilter {
    Nearest,   // 最近邻（块状）
    Bilinear   // 双线性（平滑，默认）
};

// 纹理索引环绕：把连续索引 i 映射到 [0, n)
inline int wrapTexelIndex(int i, int n, WrapMode mode) {
    if (n <= 0) return 0;
    switch (mode) {
        case WrapMode::Clamp:
            return (i < 0) ? 0 : ((i >= n) ? n - 1 : i);
        case WrapMode::Mirror: {
            if (n == 1) return 0;
            const int period = 2 * n;
            int k = i % period;
            if (k < 0) k += period;
            return (k < n) ? k : (period - 1 - k);
        }
        case WrapMode::Repeat:
        default: {
            int k = i % n;
            if (k < 0) k += n;
            return k;
        }
    }
}

// 内存中的 RGB 纹理（行优先：index = y * width + x）
class Texture {
private:
    int width_ = 0;
    int height_ = 0;
    std::vector<math::Vec3> texels_;

public:
    Texture() = default;

    // 建一张 w×h 的纹理，所有像素填 fill
    Texture(int width, int height, const math::Vec3& fill = math::Vec3(0.0f, 0.0f, 0.0f))
        : width_(width > 0 ? width : 0), height_(height > 0 ? height : 0),
          texels_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), fill) {}

    int width() const { return width_; }
    int height() const { return height_; }
    bool empty() const { return texels_.empty(); }

    // 写像素：越界忽略
    void setTexel(int x, int y, const math::Vec3& color) {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
        texels_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)] = color;
    }

    // 读像素：越界返回零向量（不做环绕 —— 需要环绕请用 sample）
    math::Vec3 texel(int x, int y) const {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) return math::Vec3();
        return texels_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)];
    }

    // 按归一化 uv 采样（返回 [0,1] 的三元组）。
    // @param u,v   归一化纹理坐标（可超出 [0,1]，按 wrap 处理）
    // @param wrap  环绕模式
    // @param filter 滤波方式
    // @note 空纹理返回零向量。Bilinear 的四个采样点**各自**做环绕 ⇒ 平铺接缝也不会取错。
    math::Vec3 sample(float u, float v, WrapMode wrap = WrapMode::Repeat,
                      TextureFilter filter = TextureFilter::Bilinear) const {
        if (empty()) return math::Vec3();

        // 归一化 uv → 连续像素索引（像素中心在 i+0.5）
        const float fx = u * static_cast<float>(width_) - 0.5f;
        const float fy = v * static_cast<float>(height_) - 0.5f;

        if (filter == TextureFilter::Nearest) {
            const int x = wrapTexelIndex(static_cast<int>(std::floor(fx + 0.5f)), width_, wrap);
            const int y = wrapTexelIndex(static_cast<int>(std::floor(fy + 0.5f)), height_, wrap);
            return texel(x, y);
        }

        const float x0f = std::floor(fx), y0f = std::floor(fy);
        const float tx = fx - x0f, ty = fy - y0f;
        const int x0 = wrapTexelIndex(static_cast<int>(x0f), width_, wrap);
        const int x1 = wrapTexelIndex(static_cast<int>(x0f) + 1, width_, wrap);
        const int y0 = wrapTexelIndex(static_cast<int>(y0f), height_, wrap);
        const int y1 = wrapTexelIndex(static_cast<int>(y0f) + 1, height_, wrap);

        const math::Vec3 c00 = texel(x0, y0);
        const math::Vec3 c10 = texel(x1, y0);
        const math::Vec3 c01 = texel(x0, y1);
        const math::Vec3 c11 = texel(x1, y1);
        const math::Vec3 top = c00 * (1.0f - tx) + c10 * tx;
        const math::Vec3 bottom = c01 * (1.0f - tx) + c11 * tx;
        return top * (1.0f - ty) + bottom * ty;
    }
};

} // namespace gfx

#endif // GFX_TEXTURE_HPP
