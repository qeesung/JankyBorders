#pragma once

#include <stddef.h>
#include <stdint.h>

#define ADAPTIVE_COLOR_IMAGE_WIDTH 128u
#define ADAPTIVE_COLOR_IMAGE_HEIGHT 128u
#define ADAPTIVE_COLOR_SIDE_COUNT 4u

#define ADAPTIVE_COLOR_ENDPOINT_SKIP 16u
#define ADAPTIVE_COLOR_OUTER_SKIP 2u
#define ADAPTIVE_COLOR_INNER_DEPTH 8u
#define ADAPTIVE_COLOR_MIN_ALPHA 128u

#define ADAPTIVE_COLOR_INITIAL_THRESHOLD 0.179
#define ADAPTIVE_COLOR_SWITCH_TO_BLACK_THRESHOLD 0.20
#define ADAPTIVE_COLOR_SWITCH_TO_WHITE_THRESHOLD 0.16

#define ADAPTIVE_COLOR_BLACK UINT32_C(0xff000000)
#define ADAPTIVE_COLOR_WHITE UINT32_C(0xffffffff)
#define ADAPTIVE_COLOR_SIDE_MASK(side) ((uint8_t)(UINT8_C(1) << (side)))

/* The order intentionally matches enum border_side in misc/drawing.h. */
enum adaptive_color_side {
  ADAPTIVE_COLOR_SIDE_TOP = 0,
  ADAPTIVE_COLOR_SIDE_RIGHT,
  ADAPTIVE_COLOR_SIDE_BOTTOM,
  ADAPTIVE_COLOR_SIDE_LEFT,
};

enum adaptive_color_status {
  ADAPTIVE_COLOR_OK = 0,
  ADAPTIVE_COLOR_INVALID_ARGUMENT,
  ADAPTIVE_COLOR_INVALID_DIMENSIONS,
  ADAPTIVE_COLOR_INVALID_STRIDE,
  ADAPTIVE_COLOR_BUFFER_TOO_SMALL,
  ADAPTIVE_COLOR_SIZE_OVERFLOW,
};

/*
 * valid_mask marks colors produced by a previous successful sample. Fallback
 * colors are deliberately kept separate so a failed sample does not turn a
 * configured fallback into hysteresis state.
 */
struct adaptive_color_cache {
  uint32_t colors[ADAPTIVE_COLOR_SIDE_COUNT];
  uint8_t valid_mask;
};

struct adaptive_color_result {
  /* Resolved colors: fresh sample, then previous cache, then fallback. */
  uint32_t colors[ADAPTIVE_COLOR_SIDE_COUNT];

  /* Only meaningful for sides present in sampled_mask. */
  double luminance[ADAPTIVE_COLOR_SIDE_COUNT];

  /* Sides with enough valid pixels in this image. */
  uint8_t sampled_mask;

  /* Sides that may be copied into adaptive_color_cache after this call. */
  uint8_t cache_mask;

  /* Sides whose resolved color differs from the previous effective color. */
  uint8_t changed_mask;
};

/*
 * Analyze a premultiplied 8-bit BGRA image. Row zero is the top row.
 *
 * The image must be exactly 128x128. pixels_size must cover every byte read;
 * row padding is supported through bytes_per_row. previous may be NULL. On an
 * error, result is left untouched.
 */
enum adaptive_color_status adaptive_color_analyze_bgra(
    const uint8_t* pixels,
    size_t pixels_size,
    size_t width,
    size_t height,
    size_t bytes_per_row,
    const struct adaptive_color_cache* previous,
    const uint32_t fallback[ADAPTIVE_COLOR_SIDE_COUNT],
    struct adaptive_color_result* result);
