// test_png.cpp —— gfx::savePng PNG 编码测试（零第三方依赖、独立解码器可验证）
//
// 运行：g++ -O2 -std=c++17 -I src/include -I tests tests/test_png.cpp -o test_png && ./test_png
// 说明：仅用相对路径 test_png_out.png 作为临时输出，测试结束删除，不依赖任何外部文件。

#include "gfx_test.hpp"
#include <gfx/render/png.hpp>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::uint32_t readBE32(const std::vector<std::uint8_t>& b, std::size_t off) {
    return (static_cast<std::uint32_t>(b[off]) << 24) |
           (static_cast<std::uint32_t>(b[off + 1]) << 16) |
           (static_cast<std::uint32_t>(b[off + 2]) << 8) |
           static_cast<std::uint32_t>(b[off + 3]);
}

std::string chunkType(const std::vector<std::uint8_t>& b, std::size_t off) {
    return std::string(reinterpret_cast<const char*>(b.data() + off), 4);
}

} // namespace

int main() {
    // 1) CRC-32 标准测试向量："123456789" → 0xCBF43926
    const std::uint8_t checkData[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_CHECK(gfx::detail::crc32(checkData, 9) == 0xCBF43926u);

    // 2) Adler-32 标准测试向量："123456789" → 0x091E01DE
    TEST_CHECK(gfx::detail::adler32(checkData, 9) == 0x091E01DEu);

    // 3) 构造 3x2 画布并写盘
    gfx::Canvas c(3, 2);
    c.putPixel(0, 0, gfx::rgb(255, 0, 0));
    c.putPixel(1, 0, gfx::rgb(0, 255, 0));
    c.putPixel(2, 0, gfx::rgb(0, 0, 255));
    c.putPixel(0, 1, gfx::rgb(10, 20, 30));
    c.putPixel(1, 1, gfx::rgb(40, 50, 60));
    c.putPixel(2, 1, gfx::rgb(70, 80, 90));

    std::string error;
    const char* path = "test_png_out.png";
    TEST_CHECK(gfx::savePng(c, path, &error));
    TEST_CHECK(error.empty());

    // 非法尺寸应失败并给出中文原因
    gfx::Canvas empty;
    std::string badError;
    TEST_CHECK(!gfx::savePng(empty, "test_png_should_not_exist.png", &badError));
    TEST_CHECK(badError == "画布尺寸非法");

    // 读回整份文件（二进制）
    std::ifstream in(path, std::ios::binary);
    TEST_CHECK(in.good());
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    in.close();
    TEST_CHECK(bytes.size() > 8);

    // 4a) 8 字节签名
    TEST_CHECK(bytes.size() >= 8);
    if (bytes.size() >= 8) {
        bool sigOk = true;
        for (int i = 0; i < 8; ++i) sigOk = sigOk && bytes[i] == gfx::PNG_SIGNATURE[i];
        TEST_CHECK(sigOk);
    }

    // 4b) IHDR 块：偏移 8 处 length=13、type="IHDR"，且字段值正确
    TEST_CHECK(readBE32(bytes, 8) == 13);
    TEST_CHECK(chunkType(bytes, 12) == "IHDR");
    TEST_CHECK(readBE32(bytes, 16) == 3);   // width
    TEST_CHECK(readBE32(bytes, 20) == 2);   // height
    TEST_CHECK(bytes[24] == 8);             // bit depth
    TEST_CHECK(bytes[25] == 2);             // color type: truecolor
    TEST_CHECK(bytes[26] == 0);             // compression
    TEST_CHECK(bytes[27] == 0);             // filter
    TEST_CHECK(bytes[28] == 0);             // interlace

    // 4c) 末尾 12 字节为空 IEND 块：length=0 + "IEND" + CRC
    TEST_CHECK(bytes.size() >= 12);
    if (bytes.size() >= 12) {
        const std::size_t tail = bytes.size() - 12;
        TEST_CHECK(readBE32(bytes, tail) == 0);
        TEST_CHECK(chunkType(bytes, tail + 4) == "IEND");
        const std::uint8_t iendData[4] = {'I', 'E', 'N', 'D'};
        TEST_CHECK(readBE32(bytes, tail + 8) == gfx::detail::crc32(iendData, 4));
    }

    // 5) 逐块遍历：重算 type+data 的 CRC 并与文件中的 CRC 比对（编码器最强自校验）
    std::size_t offset = 8;
    std::size_t idatLen = 0;
    int chunkCount = 0;
    while (offset + 12 <= bytes.size()) {
        const std::uint32_t len = readBE32(bytes, offset);
        const std::string type = chunkType(bytes, offset + 4);
        const std::size_t dataOff = offset + 8;
        if (dataOff + len + 4 > bytes.size()) { // 越界：块长度非法
            TEST_CHECK(false);
            break;
        }
        const std::uint32_t storedCrc = readBE32(bytes, dataOff + len);
        const std::uint32_t calcCrc = gfx::detail::crc32(bytes.data() + offset + 4, 4 + len);
        TEST_CHECK(storedCrc == calcCrc);
        if (type == "IDAT") idatLen = len;
        offset = dataOff + len + 4;
        ++chunkCount;
    }
    TEST_CHECK(offset == bytes.size()); // 块首尾相接、正好覆盖整个文件
    TEST_CHECK(chunkCount == 3);        // IHDR + IDAT + IEND

    // 6) 尺寸关系：文件大小 = 8 + (12+13) + (12+IDAT) + 12；IDAT 比原始像素数据更长（含块开销）
    const std::size_t rawLen = static_cast<std::size_t>(2) * (1 + static_cast<std::size_t>(3) * 3);
    TEST_CHECK(idatLen > 0);
    TEST_CHECK(idatLen > rawLen);
    TEST_CHECK(bytes.size() == 8 + (12 + 13) + (12 + idatLen) + 12);

    // 清理临时文件
    std::remove(path);

    return gfx::test::summary();
}
