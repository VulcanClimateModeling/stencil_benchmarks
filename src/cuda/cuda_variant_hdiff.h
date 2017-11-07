#pragma once

#include "hdiff_stencil_variant.h"

namespace platform {

    namespace cuda {
        template <typename ValueType>
        __global__ void kernel_hdiff(ValueType const *__restrict__ in,
            ValueType const *__restrict__ coeff,
            ValueType *__restrict__ out,
            const int isize,
            const int jsize,
            const int ksize,
            const int istride,
            const int jstride,
            const int kstride,
            const int iblocksize,
            const int jblocksize) {

            constexpr int block_halo = 1;
            constexpr int padded_boundary = block_halo;

            int ipos, jpos;
            int iblock_pos, jblock_pos;
            const int jboundary_limit = jblocksize + 2 * block_halo;
            const int iminus_limit = jboundary_limit + block_halo;
            const int iplus_limit = iminus_limit + block_halo;

            iblock_pos = -block_halo - 1;
            jblock_pos = -block_halo - 1;
            ipos = -block_halo - 1;
            jpos = -block_halo - 1;
            if (threadIdx.y < jboundary_limit) {
                ipos = blockIdx.x * iblocksize + threadIdx.x;
                jpos = (int)blockIdx.y * jblocksize + (int)threadIdx.y + -block_halo;
                iblock_pos = threadIdx.x;
                jblock_pos = (int)threadIdx.y + -block_halo;
            } else if (threadIdx.y < iminus_limit) {
                ipos = blockIdx.x * iblocksize - padded_boundary + threadIdx.x % padded_boundary;
                jpos = (int)blockIdx.y * jblocksize + (int)threadIdx.x / padded_boundary + -block_halo;
                iblock_pos = -padded_boundary + (int)threadIdx.x % padded_boundary;
                jblock_pos = (int)threadIdx.x / padded_boundary + -block_halo;
            } else if (threadIdx.y < iplus_limit) {
                ipos = blockIdx.x * iblocksize + threadIdx.x % padded_boundary + iblocksize;
                jpos = (int)blockIdx.y * jblocksize + (int)threadIdx.x / padded_boundary + -block_halo;
                iblock_pos = threadIdx.x % padded_boundary + iblocksize;
                jblock_pos = (int)threadIdx.x / padded_boundary + -block_halo;
            }

            extern __shared__ char smem[];

            const int cache_size = (iblocksize + 2 * block_halo) * (jblocksize + 2 * block_halo);
            ValueType *__restrict__ lap = reinterpret_cast<ValueType *>(&smem[0]);
            ValueType *__restrict__ flx = &lap[cache_size];
            ValueType *__restrict__ fly = &flx[cache_size];

            constexpr int cache_istride = 1;
            const int cache_jstride = iblocksize + 2 * block_halo;
            const int cache_index =
                (iblock_pos + padded_boundary) * cache_istride + (jblock_pos + block_halo) * cache_jstride;
            int index = ipos * istride + jpos * jstride;

            const int iblock_max = (blockIdx.x + 1) * iblocksize < isize ? iblocksize : isize - blockIdx.x * iblocksize;
            const int jblock_max = (blockIdx.y + 1) * jblocksize < jsize ? jblocksize : jsize - blockIdx.y * jblocksize;

            for (int k = 0; k < ksize; ++k) {
                if (iblock_pos >= -1 && iblock_pos < iblock_max + 1 && jblock_pos >= -1 &&
                    jblock_pos < jblock_max + 1) {
                    lap[cache_index] = (ValueType)4 * __ldg(&in[index]) -
                                       (__ldg(&in[index + istride]) + __ldg(&in[index - istride]) +
                                           __ldg(&in[index + jstride]) + __ldg(&in[index - jstride]));
                }

                __syncthreads();

                if (iblock_pos >= -1 && iblock_pos < iblock_max && jblock_pos >= 0 && jblock_pos < jblock_max) {
                    flx[cache_index] = lap[cache_index + cache_istride] - lap[cache_index];
                    if (flx[cache_index] * (__ldg(&in[index + istride]) - __ldg(&in[index])) > 0) {
                        flx[cache_index] = 0.;
                    }
                }

                if (iblock_pos >= 0 && iblock_pos < iblock_max && jblock_pos >= -1 && jblock_pos < jblock_max) {
                    fly[cache_index] = lap[cache_index + cache_jstride] - lap[cache_index];
                    if (fly[cache_index] * (__ldg(&in[index + jstride]) - __ldg(&in[index])) > 0) {
                        fly[cache_index] = 0.;
                    }
                }

                __syncthreads();

                if (iblock_pos >= 0 && iblock_pos < iblock_max && jblock_pos >= 0 && jblock_pos < jblock_max) {
                    out[index] = __ldg(&in[index]) -
                                 coeff[index] * (flx[cache_index] - flx[cache_index - cache_istride] +
                                                    fly[cache_index] - fly[cache_index - cache_jstride]);
                }

                index += kstride;
            }
        }

        template <class Platform, class ValueType>
        class variant_hdiff final : public hdiff_stencil_variant<Platform, ValueType> {
            static constexpr int block_halo = 1;
            static constexpr int padded_boundary = block_halo;

          public:
            using value_type = ValueType;
            using platform = Platform;
            using allocator = typename platform::template allocator<value_type>;

            variant_hdiff(const arguments_map &args)
                : hdiff_stencil_variant<Platform, ValueType>(args), m_iblocksize(args.get<int>("i-blocksize")),
                  m_jblocksize(args.get<int>("j-blocksize")) {
                if (m_iblocksize <= 0 || m_jblocksize <= 0)
                    throw ERROR("invalid block size");
                m_jblocksize += 2 * block_halo + 2;
                platform::limit_blocksize(m_iblocksize, m_jblocksize);
                m_jblocksize -= 2 * block_halo + 2;
                if ((m_jblocksize + 2 * block_halo) * padded_boundary > 32 ||
                    m_iblocksize < m_jblocksize + 2 * block_halo) {
                    std::cerr << "WARNING: reset CUDA block size to default to conform to implementation limits "
                              << "(" << m_iblocksize << "x" << m_jblocksize << " to 32x8)" << std::endl;
                    m_iblocksize = 32;
                    m_jblocksize = 8;
                }
            }
            ~variant_hdiff() {}

            void prerun() override {
                hdiff_stencil_variant<platform, value_type>::prerun();

                auto prefetch = [&](const value_type *ptr) {
                    if (cudaMemPrefetchAsync(ptr - this->zero_offset(), this->storage_size() * sizeof(value_type), 0) !=
                        cudaSuccess)
                        throw ERROR("error in cudaMemPrefetchAsync");
                };
                prefetch(this->in());
                prefetch(this->coeff());
                prefetch(this->out());

                if (cudaDeviceSynchronize() != cudaSuccess)
                    throw ERROR("error in cudaDeviceSynchronize");

                if (sizeof(value_type) == 4) {
                    if (cudaDeviceSetSharedMemConfig(cudaSharedMemBankSizeFourByte) != cudaSuccess)
                        throw ERROR("could not set shared memory bank size");
                } else if (sizeof(value_type) == 8) {
                    if (cudaDeviceSetSharedMemConfig(cudaSharedMemBankSizeEightByte) != cudaSuccess)
                        throw ERROR("could not set shared memory bank size");
                }
            }

            void hdiff() override {
                dim3 bs = blocksize();
                int smem_size = 3 * sizeof(value_type) * (bs.x + 2 * block_halo) * (bs.y + 2 * block_halo);
                bs.y += 2 * block_halo + 2;
                kernel_hdiff<<<blocks(), bs, smem_size>>>(this->in(),
                    this->coeff(),
                    this->out(),
                    this->isize(),
                    this->jsize(),
                    this->ksize(),
                    this->istride(),
                    this->jstride(),
                    this->kstride(),
                    m_iblocksize,
                    m_jblocksize);
                if (cudaDeviceSynchronize() != cudaSuccess)
                    throw ERROR("error in cudaDeviceSynchronize");
            }

          private:
            dim3 blocks() const {
                return dim3((this->isize() + m_iblocksize - 1) / m_iblocksize,
                    (this->jsize() + m_jblocksize - 1) / m_jblocksize);
            }

            dim3 blocksize() const { return dim3(m_iblocksize, m_jblocksize); }

            int m_iblocksize, m_jblocksize;
        };

    } // cuda

} // namespace platform
