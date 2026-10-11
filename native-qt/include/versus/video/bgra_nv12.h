#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace versus::video::detail {
// BT.601 limited range, with the same rounded 2x2 chroma average as the
// scalar encoder path. Handles padded input and unaligned buffers.
inline void bgraToNv12(const uint8_t *src, int width, int height, int stride, uint8_t *dst) {
    auto *uv = dst + static_cast<size_t>(width)*height;
#if defined(_M_X64) || defined(__SSE2__)
    const auto zero = _mm_setzero_si128();
    const auto yWeights = _mm_setr_epi16(25,129,66,0,25,129,66,0);
    const auto uWeights = _mm_setr_epi16(112,-74,-38,0,112,-74,-38,0);
    const auto vWeights = _mm_setr_epi16(-18,-94,112,0,-18,-94,112,0);
    const auto round = _mm_set1_epi32(128);
    const auto dot = [](__m128i pixels, __m128i weights) {
        const auto pairs = _mm_madd_epi16(pixels, weights);
        const auto sums = _mm_add_epi32(pairs, _mm_srli_si128(pairs,4));
        return _mm_shuffle_epi32(sums, _MM_SHUFFLE(2,0,2,0));
    };
    const auto store4 = [zero](uint8_t *out, __m128i values) {
        const auto bytes = _mm_packus_epi16(_mm_packs_epi32(values,zero),zero);
        const auto packed = _mm_cvtsi128_si32(bytes);
        std::memcpy(out,&packed,4);
    };
#endif
    for (int y = 0; y < height; ++y) {
        const auto *row = src + static_cast<size_t>(y)*stride;
        auto *out = dst + static_cast<size_t>(y)*width;
        int x = 0;
#if defined(_M_X64) || defined(__SSE2__)
        for (; x+3 < width; x += 4) {
            const auto pixels = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row+x*4));
            const auto low = dot(_mm_unpacklo_epi8(pixels,zero),yWeights);
            const auto high = dot(_mm_unpackhi_epi8(pixels,zero),yWeights);
            const auto sums = _mm_unpacklo_epi64(low,high);
            store4(out+x,_mm_add_epi32(_mm_srai_epi32(_mm_add_epi32(sums,round),8),_mm_set1_epi32(16)));
        }
#endif
        for (; x < width; ++x) {
            const auto *p = row+x*4;
            out[x] = static_cast<uint8_t>(((66*p[2]+129*p[1]+25*p[0]+128)>>8)+16);
        }
    }
    if ((width & 1) || (height & 1)) std::fill(uv, uv+static_cast<size_t>(width)*height/2, uint8_t(128));
    for (int y = 0; y+1 < height; y += 2) {
        const auto *row0 = src+static_cast<size_t>(y)*stride, *row1 = row0+stride;
        auto *out = uv+static_cast<size_t>(y/2)*width;
        int x = 0;
#if defined(_M_X64) || defined(__SSE2__)
        for (; x+3 < width; x += 4) {
            const auto a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row0+x*4));
            const auto b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row1+x*4));
            const auto low = _mm_add_epi16(_mm_unpacklo_epi8(a,zero),_mm_unpacklo_epi8(b,zero));
            const auto high = _mm_add_epi16(_mm_unpackhi_epi8(a,zero),_mm_unpackhi_epi8(b,zero));
            const auto sums = _mm_unpacklo_epi64(_mm_add_epi16(low,_mm_srli_si128(low,8)),
                                                 _mm_add_epi16(high,_mm_srli_si128(high,8)));
            const auto average = _mm_srli_epi16(_mm_add_epi16(sums,_mm_set1_epi16(2)),2);
            const auto chroma = _mm_unpacklo_epi32(dot(average,uWeights),dot(average,vWeights));
            store4(out+x,_mm_add_epi32(_mm_srai_epi32(_mm_add_epi32(chroma,round),8),round));
        }
#endif
        for (; x+1 < width; x += 2) {
            const auto *a = row0+x*4, *b = row1+x*4;
            const int blue=(a[0]+a[4]+b[0]+b[4]+2)/4;
            const int green=(a[1]+a[5]+b[1]+b[5]+2)/4;
            const int red=(a[2]+a[6]+b[2]+b[6]+2)/4;
            out[x]=static_cast<uint8_t>(std::clamp(((-38*red-74*green+112*blue+128)>>8)+128,0,255));
            out[x+1]=static_cast<uint8_t>(std::clamp(((112*red-94*green-18*blue+128)>>8)+128,0,255));
        }
    }
}
}
