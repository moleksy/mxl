// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace mxl::lib::fabrics::ofi
{

    /** \brief Immediate data representation for discrete flow transfers.
     */
    class ImmDataGrain
    {
    public:
        struct Unpacked //**< Unpacked representation of immediate data. */
        {
            std::uint16_t ringBufferSlot;
            std::uint16_t sliceIndex;
        };

    public:
        /** \brief Create immediate data from packed data.
         *
         * \param data The packed immediate data.
         */
        ImmDataGrain(std::uint32_t data) noexcept;

        /** \brief Create immediate data from ring buffer slot and slice index.
         *
         * The ring buffer slot must be pre-computed by the caller (typically
         * as \c grainIndex \% \c grainCount). Using a \c uint16_t parameter
         * prevents silent truncation of 64-bit grain indices that would
         * otherwise wrap after 65 536 grains (~18 minutes at 60 fps).
         *
         * \param ringBufferSlot The ring buffer slot index (grainIndex % grainCount).
         * \param sliceIndex The slice index within the grain.
         */
        ImmDataGrain(std::uint16_t ringBufferSlot, std::uint16_t sliceIndex) noexcept;

        /** \brief Unpack the immediate data into ring buffer index and slice index.
         *
         * \return A pair containing the ring buffer index and slice index.
         */
        [[nodiscard]]
        Unpacked unpack() const noexcept;

        /** \brief Get the packed immediate data.
         *
         * \return The packed immediate data.
         */
        [[nodiscard]]
        std::uint32_t data() const noexcept;

    private:
        std::uint32_t _inner; /**< Packed representation of immediate data. */
    };

}
