// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#   define MXL_HAS_NT_STORES 1
#   include <emmintrin.h> // SSE2
#else
#   define MXL_HAS_NT_STORES 0
#endif

namespace mxl::lib
{
    /**
     * \brief Copy \p len bytes from \p src to \p dst using non-temporal (streaming)
     *        stores on x86, bypassing the L2 cache.
     *
     * Unlike \c std::memcpy, the written bytes are not installed into the cache
     * hierarchy on the way out. This avoids evicting unrelated hot data when
     * writing a payload that will not be read again by the CPU soon — typically
     * a buffer that is about to be read by a NIC for an RDMA transfer. Keeping
     * the cache warm for the data the NIC and other threads actually reuse can
     * measurably improve throughput for large payloads.
     *
     * Use this only when:
     *  - the payload is larger than the CPU cache line size (small copies do not
     *    benefit and pay the \c sfence cost), and
     *  - the destination is not expected to be read by the CPU again soon (e.g.
     *    it is handed to a NIC for an RDMA read).
     *
     * For small copies, hot destinations, or when temporal locality is desired,
     * prefer \c std::memcpy.
     *
     * The copy has \c std::memcpy semantics: \p src and \p dst must not overlap.
     * On architectures without SSE2 this falls back to \c std::memcpy.
     *
     * \param dst Destination buffer.
     * \param src Source buffer.
     * \param len Number of bytes to copy.
     */
    inline void memcpyNonTemporal(void* dst, void const* src, std::size_t len) noexcept
    {
#if MXL_HAS_NT_STORES
        auto* d = static_cast<std::uint8_t*>(dst);
        auto const* s = static_cast<std::uint8_t const*>(src);

        // _mm_stream_si128 (MOVNTDQ) requires a 16-byte aligned destination, so
        // copy a small head with a regular store until the destination is aligned.
        auto const misalignment = reinterpret_cast<std::uintptr_t>(d) & std::uintptr_t{15};
        auto head = (misalignment != 0) ? (std::size_t{16} - misalignment) : std::size_t{0};
        if (head > len)
        {
            head = len;
        }
        if (head != 0)
        {
            std::memcpy(d, s, head);
            d += head;
            s += head;
            len -= head;
        }

        // Stream the aligned middle in 16-byte chunks. The source may be
        // unaligned, hence the unaligned load.
        auto const chunks = len / sizeof(__m128i);
        for (auto i = std::size_t{0}; i < chunks; ++i)
        {
            auto const v = _mm_loadu_si128(reinterpret_cast<__m128i const*>(s) + i);
            _mm_stream_si128(reinterpret_cast<__m128i*>(d) + i, v);
        }
        auto const streamed = chunks * sizeof(__m128i);
        d += streamed;
        s += streamed;
        len -= streamed;

        // Copy the remaining tail (< 16 bytes) with a regular store.
        if (len != 0)
        {
            std::memcpy(d, s, len);
        }

        // Make the non-temporal stores globally visible before any subsequent
        // reads of the destination (e.g. by an RDMA NIC).
        _mm_sfence();
#else
        std::memcpy(dst, src, len);
#endif
    }
}
