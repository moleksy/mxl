// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>
#include "ImmData.hpp"

using namespace mxl::lib::fabrics::ofi;

TEST_CASE("ofi: ImmDataGrain pack and unpack round-trip", "[ofi][ImmData]")
{
    auto immData = ImmDataGrain{uint16_t{3}, uint16_t{7}};
    auto [slot, slice] = immData.unpack();
    REQUIRE(slot == 3);
    REQUIRE(slice == 7);
}

TEST_CASE("ofi: ImmDataGrain from packed data round-trip", "[ofi][ImmData]")
{
    auto original = ImmDataGrain{uint16_t{42}, uint16_t{100}};
    auto packed = original.data();
    auto restored = ImmDataGrain{packed};
    auto [slot, slice] = restored.unpack();
    REQUIRE(slot == 42);
    REQUIRE(slice == 100);
}

TEST_CASE("ofi: ImmDataGrain max ring buffer slot", "[ofi][ImmData]")
{
    auto immData = ImmDataGrain{uint16_t{65535}, uint16_t{65535}};
    auto [slot, slice] = immData.unpack();
    REQUIRE(slot == 65535);
    REQUIRE(slice == 65535);
}

TEST_CASE("ofi: ImmDataGrain zero values", "[ofi][ImmData]")
{
    auto immData = ImmDataGrain{uint16_t{0}, uint16_t{0}};
    auto [slot, slice] = immData.unpack();
    REQUIRE(slot == 0);
    REQUIRE(slice == 0);
}
