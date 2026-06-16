// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "ImmData.hpp"
#include <cstdint>
#include <bit>

namespace mxl::lib::fabrics::ofi
{
    ImmDataGrain::ImmDataGrain(std::uint32_t data) noexcept
    {
        _inner = data;
    }

    ImmDataGrain::ImmDataGrain(std::uint16_t ringBufferSlot, std::uint16_t sliceIndex) noexcept
    {
        _inner = std::bit_cast<std::uint32_t>(Unpacked{.ringBufferSlot = ringBufferSlot, .sliceIndex = sliceIndex});
    }

    ImmDataGrain::Unpacked ImmDataGrain::unpack() const noexcept
    {
        return std::bit_cast<Unpacked>(_inner);
    }

    std::uint32_t ImmDataGrain::data() const noexcept
    {
        return _inner;
    }
}
