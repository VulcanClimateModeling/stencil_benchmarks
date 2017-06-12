/*
  GridTools Libraries

  Copyright (c) 2017, ETH Zurich and MeteoSwiss
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:

  1. Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

  3. Neither the name of the copyright holder nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  For information: http://eth-cscs.github.io/gridtools/
*/

#pragma once

#include "definitions.hpp"
#include "nano_array.hpp"
#include "storage_info_metafunctions.hpp"

namespace gridtools {

    /**
     * @brief This struct is used to pass alignment information to storage info types.
     * @tparam alignment value
     */
    template < unsigned N >
    struct alignment {
        const static unsigned value = N;
    };

    template < typename T >
    struct is_alignment : boost::mpl::false_ {};

    template < unsigned N >
    struct is_alignment< alignment< N > > : boost::mpl::true_ {};

    /**
     * @brief This struct is internally in the storage info class. In case of a given alignment
     * additional fields have to be stored (e.g., unaligned dimensions, unaligned strides, etc.).
     * @tparam Alignment alignment type
     * @tparam Layout layout map type
     * @tparam Halo halo type
     */
    template < typename Alignment, typename Layout, typename Halo >
    struct alignment_impl;

    template < unsigned M, typename Layout, typename Halo >
    struct alignment_impl< alignment< M >, Layout, Halo > {
        static_assert((M > 1), "Wrong alignment value passed.");
        static constexpr unsigned N = Layout::masked_length;
        static constexpr unsigned InitialOffset = get_initial_offset< Layout, alignment< M >, Halo >::compute();

        nano_array< unsigned, N > m_unaligned_dims;
        nano_array< unsigned, N > m_unaligned_strides;

        constexpr alignment_impl(nano_array< unsigned, N > dims, nano_array< unsigned, N > strides)
            : m_unaligned_dims(dims), m_unaligned_strides(strides) {}

        template < unsigned Coord >
        GT_FUNCTION constexpr unsigned unaligned_dim() const {
            return m_unaligned_dims[Coord];
        }

        template < unsigned Coord >
        GT_FUNCTION constexpr unsigned unaligned_stride() const {
            return m_unaligned_strides[Coord];
        }
    };

    /* specialization for unaligned storage infos */
    template < typename Layout, typename Halo >
    struct alignment_impl< alignment< 1 >, Layout, Halo > {
        static constexpr unsigned InitialOffset = 0;
        template < typename... T >
        constexpr alignment_impl(T... t) {}

        template < unsigned Coord >
        GT_FUNCTION constexpr unsigned unaligned_dim() const {
            return 0;
        }

        template < unsigned Coord >
        GT_FUNCTION constexpr unsigned unaligned_stride() const {
            return 0;
        }
    };
}