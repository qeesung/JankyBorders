#pragma once

#include <stdbool.h>
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
#define ADAPTIVE_COLOR_FOCUS_ON_LIGHT UINT32_C(0xffe5484d)
#define ADAPTIVE_COLOR_FOCUS_ON_DARK UINT32_C(0xffe1e3e4)
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
 * A palette maps sampled background luminance to a border color. on_light is
 * used for a light background and on_dark for a dark background. The lower
 * and upper switch thresholds form a hysteresis band around initial_threshold.
 * A uniform palette resolves one color from the median of the valid edge
 * luminances and applies that color to the complete border.
 */
struct adaptive_color_palette {
  uint32_t on_light;
  uint32_t on_dark;
  double initial_threshold;
  double switch_to_on_light_threshold;
  double switch_to_on_dark_threshold;
  bool uniform;
};

extern const struct adaptive_color_palette ADAPTIVE_COLOR_PALETTE_BLACK_WHITE;
extern const struct adaptive_color_palette ADAPTIVE_COLOR_PALETTE_FOCUS;

/*
 * valid_mask marks colors produced by a previous successful sample. Fallback
 * colors are deliberately kept separate so a failed sample does not turn a
 * configured fallback into hysteresis state.
 */
struct adaptive_color_cache {
  uint32_t colors[ADAPTIVE_COLOR_SIDE_COUNT];
  uint8_t valid_mask;
};

/*
 * Uniform focus colors use this state to reject a one-frame color change.
 * A different color must be returned by two successive successful samples
 * before replacing the last known-good cache.
 */
struct adaptive_color_switch_confirmation {
  uint32_t candidate;
  bool pending;
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

/*
 * Palette-aware entry point. A cached color participates in hysteresis only
 * when it matches one of the supplied palette colors. An unknown cached color
 * is discarded, so a fresh sample uses the initial threshold and an invalid
 * side uses its configured fallback rather than stale palette state.
 */
enum adaptive_color_status adaptive_color_analyze_bgra_with_palette(
    const uint8_t* pixels,
    size_t pixels_size,
    size_t width,
    size_t height,
    size_t bytes_per_row,
    const struct adaptive_color_palette* palette,
    const struct adaptive_color_cache* previous,
    const uint32_t fallback[ADAPTIVE_COLOR_SIDE_COUNT],
    struct adaptive_color_result* result);

/* Select one palette color for an already-computed linear-sRGB luminance. */
enum adaptive_color_status adaptive_color_select(
    const struct adaptive_color_palette* palette,
    double luminance,
    int has_previous,
    uint32_t previous,
    uint32_t* color);

/* Return whether cache is complete, uniform, and belongs to palette. */
bool adaptive_color_uniform_cache_color(
    const struct adaptive_color_palette* palette,
    const struct adaptive_color_cache* cache,
    uint32_t* color);

void adaptive_color_switch_confirmation_reset(
    struct adaptive_color_switch_confirmation* confirmation);

/*
 * Return true when proposed may replace current. Initial colors and unchanged
 * colors are accepted immediately; a different color needs two matching calls.
 */
bool adaptive_color_switch_confirmation_accept(
    struct adaptive_color_switch_confirmation* confirmation,
    bool current_valid,
    uint32_t current,
    uint32_t proposed);
