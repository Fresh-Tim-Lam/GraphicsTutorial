// png.hpp —— gfx 图像输出：PNG 编码（零第三方依赖）
//
// 用途：把 Canvas 像素缓冲写成 PNG 文件；自行实现 CRC32/Adler32 与 zlib 存储块（stored deflate），
//       不引入 zlib/libpng 等第三方库。
// 目标读者：gfx 像素模块使用者与实现者（#include <gfx/render/png.hpp>）。
// 关联文档：docs/design/adr-0005-raster-polygon-fill.md；gfx/core/canvas.hpp；gfx/core/color.hpp
//
// 状态：生效中（2026-09-14）

#ifndef GFX_PNG_HPP
#define GFX_PNG_HPP

#include "gfx/core/canvas.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace gfx {

// PNG 文件签名（RFC 2083 §3.1）：固定 8 字节，用于快速识别文件类型。
inline constexpr std::uint8_t PNG_SIGNATURE[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

namespace detail {

// CRC-32（IEEE 802.3，即 PNG/zlib 采用的 CRC）：反射输入输出，初值 0xFFFFFFFF，
// 结果末异或 0xFFFFFFFF，多项式 0xEDB88320（0x04C11DB7 的反射形式）。
inline std::uint32_t crc32(const std::uint8_t* data, std::size_t n) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            // 最低位为 1 则右移后异或多项式，否则仅右移
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

// Adler-32（RFC 1950 §9）：对字节流维护两个 16 位累加和，模 65521（小于 2^16 的最大素数）。
inline std::uint32_t adler32(const std::uint8_t* data, std::size_t n) {
    constexpr std::uint32_t MOD = 65521u;
    std::uint32_t a = 1; // 低频字节和
    std::uint32_t b = 0; // 累加的高频和
    for (std::size_t i = 0; i < n; ++i) {
        a = (a + data[i]) % MOD;
        b = (b + a) % MOD;
    }
    return (b << 16) | a;
}

// 以 4 字节大端（网络序）写入 PNG 整数
inline void appendBE32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

// 追加一个 PNG 块：length(4, 大端) + type(4) + data + CRC32(4, 大端)。
// CRC 的计算范围是 type + data，不含 length 字段（RFC 2083 §3.2）。
inline void appendChunk(std::vector<std::uint8_t>& out, const char type[4],
                        const std::uint8_t* data, std::size_t n) {
    appendBE32(out, static_cast<std::uint32_t>(n));
    const std::size_t crcStart = out.size();
    out.push_back(static_cast<std::uint8_t>(type[0]));
    out.push_back(static_cast<std::uint8_t>(type[1]));
    out.push_back(static_cast<std::uint8_t>(type[2]));
    out.push_back(static_cast<std::uint8_t>(type[3]));
    for (std::size_t i = 0; i < n; ++i) out.push_back(data[i]);
    const std::uint32_t crc = crc32(out.data() + crcStart, 4 + n);
    appendBE32(out, crc);
}

// 把原始字节包成合法 zlib 流：2 字节 zlib 头 + 若干 deflate 存储块 + 4 字节 Adler-32（大端）。
// 存储块（BTYPE=00）不做压缩，仅按字节对齐把数据原样搬运，最大 LEN = 65535。
inline std::vector<std::uint8_t> makeZlibStored(const std::vector<std::uint8_t>& raw) {
    std::vector<std::uint8_t> z;
    z.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
    // zlib 头：CM=8（deflate）、CINFO=7（32K 窗口），FLG 使 (CMF<<8|FLG) % 31 == 0 → 0x78 0x01
    z.push_back(0x78);
    z.push_back(0x01);

    std::size_t offset = 0;
    do {
        const std::size_t remaining = raw.size() - offset;
        const std::size_t blockLen = remaining < 65535 ? remaining : 65535;
        const bool isLast = (offset + blockLen) >= raw.size();
        // 块头 1 字节：bit0 = BFINAL，bit1..2 = BTYPE(00)，高 5 位补 0
        z.push_back(isLast ? 0x01 : 0x00);
        const std::uint16_t nlen = static_cast<std::uint16_t>(~blockLen);
        z.push_back(static_cast<std::uint8_t>(blockLen & 0xFFu));         // LEN 低字节
        z.push_back(static_cast<std::uint8_t>((blockLen >> 8) & 0xFFu));  // LEN 高字节
        z.push_back(static_cast<std::uint8_t>(nlen & 0xFFu));             // NLEN 低字节
        z.push_back(static_cast<std::uint8_t>((nlen >> 8) & 0xFFu));      // NLEN 高字节
        for (std::size_t i = 0; i < blockLen; ++i) z.push_back(raw[offset + i]);
        offset += blockLen;
    } while (offset < raw.size());

    // 末尾 Adler-32，覆盖未压缩的原始字节
    appendBE32(z, adler32(raw.data(), raw.size()));
    return z;
}

// 创建输出路径的父目录（递归）；失败静默忽略，交由随后的 fopen 给出确切错误。
// @note 默认输出常写成 "output/xxx.png"，而该目录可能还不存在 —— 与其让调用方到处 mkdir，
//       不如在这里补上（教学库里这是省心的行为）。
inline void ensureParentDirectory(const char* path) {
    const std::string p(path);
    const std::size_t sep = p.find_last_of("/\\");
    if (sep == std::string::npos || sep == 0) return;
    std::error_code ec;
    std::filesystem::create_directories(p.substr(0, sep), ec);
}

} // namespace detail

// 把画布写成 8bit/通道 的 RGB PNG（alpha 通道被忽略：canvas 像素一律按不透明写出）。
// @param path 输出文件路径（UTF-8；内部用 std::fopen 的窄字符接口）
// @param error 失败时写入人类可读的中文原因（可为 nullptr）
// @return 成功 true
inline bool savePng(const Canvas& canvas, const char* path, std::string* error = nullptr) {
    if (canvas.width() <= 0 || canvas.height() <= 0) {
        if (error) *error = "画布尺寸非法";
        return false;
    }

    const int w = canvas.width();
    const int h = canvas.height();
    const Color* pixels = canvas.data();

    // 原始图像数据：每行前置 1 字节 filter 类型（0 = None），随后按 R,G,B 顺序写 w 个像素。
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(h) * (1 + static_cast<std::size_t>(w) * 3));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0x00);
        for (int x = 0; x < w; ++x) {
            const Color& c = pixels[static_cast<std::size_t>(y) * w + x];
            raw.push_back(c.r);
            raw.push_back(c.g);
            raw.push_back(c.b);
        }
    }

    // IHDR 数据 13 字节：宽高(大端) + 位深 8 + 颜色类型 2(truecolor) + 压缩/滤波/隔行 均 0
    std::uint8_t ihdr[13];
    ihdr[0] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(w) >> 24) & 0xFFu);
    ihdr[1] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(w) >> 16) & 0xFFu);
    ihdr[2] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(w) >> 8) & 0xFFu);
    ihdr[3] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(w) & 0xFFu);
    ihdr[4] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(h) >> 24) & 0xFFu);
    ihdr[5] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(h) >> 16) & 0xFFu);
    ihdr[6] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(h) >> 8) & 0xFFu);
    ihdr[7] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(h) & 0xFFu);
    ihdr[8] = 8; // bit depth
    ihdr[9] = 2; // color type: truecolor RGB
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;

    const std::vector<std::uint8_t> idat = detail::makeZlibStored(raw);

    // 组装完整文件：签名 + IHDR + IDAT + IEND
    std::vector<std::uint8_t> file;
    file.reserve(8 + (12 + 13) + (12 + idat.size()) + 12);
    file.insert(file.end(), PNG_SIGNATURE, PNG_SIGNATURE + 8);
    detail::appendChunk(file, "IHDR", ihdr, sizeof(ihdr));
    detail::appendChunk(file, "IDAT", idat.data(), idat.size());
    detail::appendChunk(file, "IEND", nullptr, 0);

    detail::ensureParentDirectory(path);
    std::FILE* fp = std::fopen(path, "wb");
    if (!fp) {
        if (error) *error = std::string("无法打开输出文件：") + path;
        return false;
    }
    bool ok = true;
    if (std::fwrite(file.data(), 1, file.size(), fp) != file.size()) {
        if (error) *error = std::string("写入文件失败：") + path;
        ok = false;
    }
    std::fclose(fp);
    return ok;
}

} // namespace gfx

#endif // GFX_PNG_HPP
