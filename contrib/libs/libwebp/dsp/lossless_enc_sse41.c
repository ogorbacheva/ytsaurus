// Copyright 2015 Google Inc. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the COPYING file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS. All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.
// -----------------------------------------------------------------------------
//
// SSE4.1 variant of methods for lossless encoder
//
// Author: Skal (pascal.massimino@gmail.com)

#include "./dsp.h"

#if defined(WEBP_USE_SSE41)
#include <assert.h>
#include <smmintrin.h>
#include "./lossless.h"

// For sign-extended multiplying constants, pre-shifted by 5:
#define CST_5b(X)  (((int16_t)((uint16_t)(X) << 8)) >> 5)

//------------------------------------------------------------------------------
// Subtract-Green Transform

static void SubtractGreenFromBlueAndRed_SSE41(uint32_t* argb_data,
                                              int num_pixels) {
  int i;
  const __m128i kCstShuffle = _mm_set_epi8(-1, 13, -1, 13, -1, 9, -1, 9,
                                           -1,  5, -1,  5, -1, 1, -1, 1);
  for (i = 0; i + 4 <= num_pixels; i += 4) {
    const __m128i in = _mm_loadu_si128((__m128i*)&argb_data[i]);
    const __m128i in_0g0g = _mm_shuffle_epi8(in, kCstShuffle);
    const __m128i out = _mm_sub_epi8(in, in_0g0g);
    _mm_storeu_si128((__m128i*)&argb_data[i], out);
  }
  // fallthrough and finish off with plain-C
  if (i != num_pixels) {
    VP8LSubtractGreenFromBlueAndRed_C(argb_data + i, num_pixels - i);
  }
}

//------------------------------------------------------------------------------
// Color Transform

#define MK_CST_16(HI, LO) \
  _mm_set1_epi32((int)(((uint32_t)(HI) << 16) | ((LO) & 0xffff)))

static void CollectColorBlueTransforms_SSE41(const uint32_t* argb, int stride,
                                             int tile_width, int tile_height,
                                             int green_to_blue, int red_to_blue,
                                             int histo[]) {
  const __m128i mult =
      MK_CST_16(CST_5b(red_to_blue) + 256,CST_5b(green_to_blue));
  const __m128i perm =
      _mm_setr_epi8(-1, 1, -1, 2, -1, 5, -1, 6, -1, 9, -1, 10, -1, 13, -1, 14);
  if (tile_width >= 4) {
    int y;
    for (y = 0; y < tile_height; ++y) {
      const uint32_t* const src = argb + y * stride;
      const __m128i A1 = _mm_loadu_si128((const __m128i*)src);
      const __m128i B1 = _mm_shuffle_epi8(A1, perm);
      const __m128i C1 = _mm_mulhi_epi16(B1, mult);
      const __m128i D1 = _mm_sub_epi16(A1, C1);
      __m128i E = _mm_add_epi16(_mm_srli_epi32(D1, 16), D1);
      int x;
      for (x = 4; x + 4 <= tile_width; x += 4) {
        const __m128i A2 = _mm_loadu_si128((const __m128i*)(src + x));
        __m128i B2, C2, D2;
        ++histo[_mm_extract_epi8(E,  0)];
        B2 = _mm_shuffle_epi8(A2, perm);
        ++histo[_mm_extract_epi8(E,  4)];
        C2 = _mm_mulhi_epi16(B2, mult);
        ++histo[_mm_extract_epi8(E,  8)];
        D2 = _mm_sub_epi16(A2, C2);
        ++histo[_mm_extract_epi8(E, 12)];
        E = _mm_add_epi16(_mm_srli_epi32(D2, 16), D2);
      }
      ++histo[_mm_extract_epi8(E,  0)];
      ++histo[_mm_extract_epi8(E,  4)];
      ++histo[_mm_extract_epi8(E,  8)];
      ++histo[_mm_extract_epi8(E, 12)];
    }
  }
  {
    const int left_over = tile_width & 3;
    if (left_over > 0) {
      VP8LCollectColorBlueTransforms_C(argb + tile_width - left_over, stride,
                                       left_over, tile_height,
                                       green_to_blue, red_to_blue, histo);
    }
  }
}

static void CollectColorRedTransforms_SSE41(const uint32_t* argb, int stride,
                                            int tile_width, int tile_height,
                                            int green_to_red, int histo[]) {

  const __m128i mult = MK_CST_16(0, CST_5b(green_to_red));
  const __m128i mask_g = _mm_set1_epi32(0x0000ff00);
  if (tile_width >= 4) {
    int y;
    for (y = 0; y < tile_height; ++y) {
      const uint32_t* const src = argb + y * stride;
      const __m128i A1 = _mm_loadu_si128((const __m128i*)src);
      const __m128i B1 = _mm_and_si128(A1, mask_g);
      const __m128i C1 = _mm_madd_epi16(B1, mult);
      __m128i D = _mm_sub_epi16(A1, C1);
      int x;
      for (x = 4; x + 4 <= tile_width; x += 4) {
        const __m128i A2 = _mm_loadu_si128((const __m128i*)(src + x));
        __m128i B2, C2;
        ++histo[_mm_extract_epi8(D,  2)];
        B2 = _mm_and_si128(A2, mask_g);
        ++histo[_mm_extract_epi8(D,  6)];
        C2 = _mm_madd_epi16(B2, mult);
        ++histo[_mm_extract_epi8(D, 10)];
        ++histo[_mm_extract_epi8(D, 14)];
        D = _mm_sub_epi16(A2, C2);
      }
      ++histo[_mm_extract_epi8(D,  2)];
      ++histo[_mm_extract_epi8(D,  6)];
      ++histo[_mm_extract_epi8(D, 10)];
      ++histo[_mm_extract_epi8(D, 14)];
    }
  }
  {
    const int left_over = tile_width & 3;
    if (left_over > 0) {
      VP8LCollectColorRedTransforms_C(argb + tile_width - left_over, stride,
                                      left_over, tile_height, green_to_red,
                                      histo);
    }
  }
}

#undef MK_CST_16

//------------------------------------------------------------------------------
// Entry point

extern void VP8LEncDspInitSSE41(void);

WEBP_TSAN_IGNORE_FUNCTION void VP8LEncDspInitSSE41(void) {
  VP8LSubtractGreenFromBlueAndRed = SubtractGreenFromBlueAndRed_SSE41;
  VP8LCollectColorBlueTransforms = CollectColorBlueTransforms_SSE41;
  VP8LCollectColorRedTransforms = CollectColorRedTransforms_SSE41;
}

#else  // !WEBP_USE_SSE41

WEBP_DSP_INIT_STUB(VP8LEncDspInitSSE41)

#endif  // WEBP_USE_SSE41
