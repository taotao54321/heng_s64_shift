/**
 * 符号付き整数型の値 x に対するシフト演算:
 *
 *   * シフト量 n が負または sizeof(x) 以上なら未定義動作。
 *     以下、n は正しいものとする。
 *   * x >= 0 のとき、x << n は x * 2**n となる。オーバーフロー時は未定義動作。
 *   * x >= 0 のとき、x >> n は x / 2**n の商となる。
 *   * x <  0 のとき、x << n は未定義動作。
 *   * x <  0 のとき、x >> n は処理系定義。
 *     (普通は算術シフト/論理シフトのどちらかになる)
 *
 * 参考: http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1256.pdf 6.5.7
 */

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <random>
#include <utility>

#include <fmt/core.h>

using s32b = std::int32_t;
using u32b = std::uint32_t;
using s64b = std::int64_t;
using u64b = std::uint64_t;

// 変愚蛮怒 v2.2.1 のコード。
// V1 が上位32bit, V2 が下位32bit, N がシフト量。
// 型は V1 が int32_t, V2 が uint32_t, N が int を想定している。
//
// シフト量は 1..=31 のみ想定している。
// シフト量 0 を渡すと 32 回シフトが発生して未定義動作となることに注意。
//
// 64bit負数を渡すことは想定していない。呼び出し元を見ると全て非負数を渡している。
// 規格的にも V1 を左シフトしているので負数を渡すと未定義動作となる。
//
// s64b_RSHIFT_1() は (V1<<(32-(N))) の部分で未定義動作が起こりうる。
// 例えば、V1 != 0 && N == 1 だと V1 を 31 回左シフトしてオーバーフローする。
// clang-format off
//#define s64b_LSHIFT_1(V1, V2, N) {V1 = (V1<<(N)) | (V2>>(32-(N))); V2 <<= (N);}
//#define s64b_RSHIFT_1(V1, V2, N) {V2 = (V1<<(32-(N))) | (V2>>(N)); V1 >>= (N);}
// clang-format on

// 右シフト時の未定義動作を回避するコード。
// clang-format off
#define s64b_LSHIFT_2(V1, V2, N) {V1 = (V1<<(N)) | (V2>>(32-(N))); V2 <<= (N);}
#define s64b_RSHIFT_2(V1, V2, N) {V2 = ((u32b)V1<<(32-(N))) | (V2>>(N)); V1 >>= (N);}
// clang-format on

namespace {

// 関数版左シフト。
//
// マクロに対する優位性:
//
//   * シフト量 0 を渡しても壊れない
//   * 型チェックができる
//   * 必要に応じて assert などを追加しやすい
void s64b_lshift(s32b* hi, u32b* lo, const int n) {
    if (n == 0) return;

    *hi = (s32b)((u32b)(*hi << n) | (*lo >> (32 - n)));
    *lo <<= n;
}

// 関数版右シフト。
void s64b_rshift(s32b* hi, u32b* lo, const int n) {
    if (n == 0) return;

    *lo = ((u32b)*hi << (32 - n)) | (*lo >> n);
    *hi >>= n;
}

std::pair<s32b, u32b> unpack_s64b(const s64b x) {
    s32b hi = (s32b)(x >> 32);
    u32b lo = (u32b)(x & ((INT64_C(1) << 32) - 1));
    return { hi, lo };
}

std::pair<s32b, u32b> f_lshift_macro(const s64b x, const int n) {
    auto [hi, lo] = unpack_s64b(x);
    s64b_LSHIFT_2(hi, lo, n);
    return { hi, lo };
}

std::pair<s32b, u32b> f_rshift_macro(const s64b x, const int n) {
    auto [hi, lo] = unpack_s64b(x);
    s64b_RSHIFT_2(hi, lo, n);
    return { hi, lo };
}

std::pair<s32b, u32b> f_lshift(const s64b x, const int n) {
    auto [hi, lo] = unpack_s64b(x);
    s64b_lshift(&hi, &lo, n);
    return { hi, lo };
}

std::pair<s32b, u32b> f_rshift(const s64b x, const int n) {
    auto [hi, lo] = unpack_s64b(x);
    s64b_rshift(&hi, &lo, n);
    return { hi, lo };
}

void check_lshift(const s64b x, const int n) {
    const auto [hi, lo] = f_lshift(x, n);
    const auto [hi_m, lo_m] = f_lshift_macro(x, n);
    if (std::tie(hi, lo) != std::tie(hi_m, lo_m))
        fmt::print("lshift error: x={}, n={}, hi={}, lo={}, hi_m={}, lo_m={}\n", x, n, hi, lo, hi_m, lo_m);
}

void check_rshift(const s64b x, const int n) {
    const auto [hi, lo] = f_rshift(x, n);
    const auto [hi_m, lo_m] = f_rshift_macro(x, n);
    if (std::tie(hi, lo) != std::tie(hi_m, lo_m))
        fmt::print("rshift error: x={}, n={}, hi={}, lo={}, hi_m={}, lo_m={}\n", x, n, hi, lo, hi_m, lo_m);
}

// x << n がオーバーフローしない最大の n を求める(ただし最大 31)。
int lshift_count_max(const s64b x) {
    if (x == 0) return 31;
    return std::min(31, __builtin_clzll((u64b)x) - 1);
}

template <class URBG>
void test(URBG& rng, std::uniform_int_distribution<s64b> dist_x) {
    std::uniform_int_distribution<int> dist_nr(1, 31);

    for (int i = 0; i < 100000; ++i) {
        const auto x = dist_x(rng);

        // シフト量 0 のテスト。
        assert(f_lshift(x, 0) == unpack_s64b(x));
        assert(f_rshift(x, 0) == unpack_s64b(x));

        // 左シフト 1..=lshift_count_max(x) 回のテスト。
        for (int n = 1; n <= lshift_count_max(x); ++n)
            check_lshift(x, n);

        // 右シフト 1..=31 回のテスト。
        for (int n = 1; n <= 31; ++n)
            check_rshift(x, n);
    }
}

} // anonymous namespace

int main() {
    check_lshift(0, 1);
    check_lshift(0, 31);
    check_rshift(0, 1);
    check_rshift(0, 31);

    std::default_random_engine rng(std::random_device {}());
    test(rng, std::uniform_int_distribution<s64b>(0, std::numeric_limits<s64b>::max()));
    test(rng, std::uniform_int_distribution<s64b>(0, 1LL << 16));

    return 0;
}
