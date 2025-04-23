#include <emmintrin.h>
#include <stdint.h>

static inline __m128i _MM_PACKUS_EPI32(const __m128i* low, const __m128i* high)
{
    const __m128i val_32 = _mm_set1_epi32(0x8000);
    const __m128i val_16 = _mm_set1_epi16(0x8000);
    const __m128i low1 = _mm_sub_epi32(*low, val_32);
    const __m128i high1 = _mm_sub_epi32(*high, val_32);
    return _mm_add_epi16(_mm_packs_epi32(low1, high1), val_16);
}

void planar_yuv_sse2(uint16_t* dstp_y, uint16_t* dstp_u, uint16_t* dstp_v, uint16_t* srcp_y, uint16_t* srcp_uv, const int dst_stride_y,
    const int dst_stride_uv, const int src_stride_y, const int src_stride_uv, const int width_y, const int width_uv, const int height_y,
    const int height_uv)
{
    for (int y = 0; y < height_y; y++) {
        for (int x = 0; x < width_y; x += 8) {
            __m128i yy = _mm_load_si128((const __m128i*)(srcp_y + x));
            yy = _mm_srli_epi16(yy, 6);
            _mm_stream_si128((__m128i*)(dstp_y + x), yy);
        }
        srcp_y += src_stride_y;
        dstp_y += dst_stride_y;
    }

    const __m128i mask = _mm_set1_epi32(0x0000FFFF);
    for (int y = 0; y < height_uv; y++) {
        for (int x = 0; x < width_uv; x += 8) {
            __m128i uv_low = _mm_load_si128((__m128i*)((uint32_t*)srcp_uv + x + 0));
            __m128i uv_high = _mm_load_si128((__m128i*)((uint32_t*)srcp_uv + x + 4));

            __m128i u_low = _mm_and_si128(uv_low, mask);
            __m128i u_high = _mm_and_si128(uv_high, mask);
            __m128i u = _MM_PACKUS_EPI32(&u_low, &u_high);
            u = _mm_srli_epi16(u, 6);
            _mm_stream_si128((__m128i*)(dstp_u + x), u);

            __m128i v_low = _mm_srli_epi32(uv_low, 16);
            __m128i v_high = _mm_srli_epi32(uv_high, 16);
            __m128i v = _MM_PACKUS_EPI32(&v_low, &v_high);
            v = _mm_srli_epi16(v, 6);
            _mm_stream_si128((__m128i*)(dstp_v + x), v);
        }
        srcp_uv += src_stride_uv;
        dstp_u += dst_stride_uv;
        dstp_v += dst_stride_uv;
    }
}
