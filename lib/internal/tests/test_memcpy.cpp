// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "mxl-internal/Memcpy.hpp"

using namespace mxl::lib;

namespace
{
    std::vector<std::uint8_t> makePattern(std::size_t len)
    {
        auto v = std::vector<std::uint8_t>(len);
        for (auto i = std::size_t{0}; i < len; ++i)
        {
            v[i] = static_cast<std::uint8_t>((i * 131u + 17u) & 0xFFu);
        }
        return v;
    }
}

TEST_CASE("memcpyNonTemporal matches std::memcpy across sizes", "[memcpy]")
{
    // Cover sub-16, exactly-16, head+middle+tail and multi-page sizes.
    for (auto len : {std::size_t{0},
             std::size_t{1},
             std::size_t{8},
             std::size_t{15},
             std::size_t{16},
             std::size_t{17},
             std::size_t{31},
             std::size_t{63},
             std::size_t{64},
             std::size_t{255},
             std::size_t{4096},
             std::size_t{4097},
             std::size_t{65537}})
    {
        auto const src = makePattern(len);
        auto dst = std::vector<std::uint8_t>(len, 0xCD);

        memcpyNonTemporal(dst.data(), src.data(), len);

        REQUIRE(dst == src);
    }
}

TEST_CASE("memcpyNonTemporal handles unaligned source and destination", "[memcpy]")
{
    constexpr auto payload = std::size_t{4096};

    // Over-allocate so we can slide the start address through every 16-byte
    // alignment, exercising the head-alignment and tail paths.
    auto const src = makePattern(payload + 32);
    auto backing = std::vector<std::uint8_t>(payload + 32, 0xAB);

    for (auto srcOff = std::size_t{0}; srcOff < 16; ++srcOff)
    {
        for (auto dstOff = std::size_t{0}; dstOff < 16; ++dstOff)
        {
            std::fill(backing.begin(), backing.end(), std::uint8_t{0xAB});

            memcpyNonTemporal(backing.data() + dstOff, src.data() + srcOff, payload);

            for (auto i = std::size_t{0}; i < payload; ++i)
            {
                REQUIRE(backing[dstOff + i] == src[srcOff + i]);
            }
            // Bytes before and after the destination range must be untouched.
            REQUIRE(backing.front() == (dstOff == 0 ? src[srcOff] : std::uint8_t{0xAB}));
            REQUIRE(backing[dstOff + payload] == std::uint8_t{0xAB});
        }
    }
}

TEST_CASE("memcpyNonTemporal with zero length is a no-op", "[memcpy]")
{
    auto dst = std::vector<std::uint8_t>{1, 2, 3, 4};
    auto const expected = dst;
    auto const src = std::vector<std::uint8_t>{9, 9, 9, 9};

    memcpyNonTemporal(dst.data(), src.data(), 0);

    REQUIRE(dst == expected);
}
