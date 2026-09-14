// gfx_test.hpp —— 自写轻量断言框架（零第三方依赖）
//
// 用途：供本项目 tests/ 使用极简断言与测试登记。设计取舍见
//       docs/design/adr-0001-gfx-math-canvas.md（D4）。
// 目标读者：编写测试的开发者与 AI Agent。
// 关联文档：tests/CMakeLists.txt；docs/guides/operations.md
//
// 状态：生效中（2026-09-09）

#ifndef GFX_TEST_HPP
#define GFX_TEST_HPP

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace gfx::test {

struct Registry {
    static int& failures() { static int f = 0; return f; }
    static int& checks()   { static int c = 0; return c; }
};

inline bool checkImpl(bool cond, const char* expr, const char* file, int line) {
    ++Registry::checks();
    if (!cond) {
        ++Registry::failures();
        std::fprintf(stderr, "  [FAIL] %s:%d: %s\n", file, line, expr);
    }
    return cond;
}

inline bool nearImpl(float a, float b, float eps, const char* exprA, const char* exprB,
                     const char* file, int line) {
    ++Registry::checks();
    if (!(std::fabs(a - b) <= eps)) {
        ++Registry::failures();
        std::fprintf(stderr, "  [FAIL] %s:%d: |%s - %s| > %g  (%.6g vs %.6g)\n",
                     file, line, exprA, exprB, eps, static_cast<double>(a), static_cast<double>(b));
        return false;
    }
    return true;
}

// 返回进程退出码（供 main 使用）
inline int summary() {
    std::printf("%d checks, %d failures\n", Registry::checks(), Registry::failures());
    return Registry::failures() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace gfx::test

#define TEST_CHECK(expr)     ::gfx::test::checkImpl(!!(expr), #expr, __FILE__, __LINE__)
#define TEST_NEAR(a, b, eps) ::gfx::test::nearImpl((a), (b), (eps), #a, #b, __FILE__, __LINE__)

#endif // GFX_TEST_HPP
