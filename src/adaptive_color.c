#include "adaptive_color.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ADAPTIVE_COLOR_MAX_SIDE_SAMPLES \
  ((ADAPTIVE_COLOR_IMAGE_WIDTH - 2u * ADAPTIVE_COLOR_ENDPOINT_SKIP) \
   * ADAPTIVE_COLOR_INNER_DEPTH)
#define ADAPTIVE_COLOR_ALL_SIDES_MASK \
  ((uint8_t)((1u << ADAPTIVE_COLOR_SIDE_COUNT) - 1u))

const struct adaptive_color_palette ADAPTIVE_COLOR_PALETTE_BLACK_WHITE = {
  .on_light = ADAPTIVE_COLOR_BLACK,
  .on_dark = ADAPTIVE_COLOR_WHITE,
  .initial_threshold = ADAPTIVE_COLOR_INITIAL_THRESHOLD,
  .switch_to_on_light_threshold = ADAPTIVE_COLOR_SWITCH_TO_BLACK_THRESHOLD,
  .switch_to_on_dark_threshold = ADAPTIVE_COLOR_SWITCH_TO_WHITE_THRESHOLD,
  .uniform = false,
};

const struct adaptive_color_palette ADAPTIVE_COLOR_PALETTE_FOCUS = {
  .on_light = ADAPTIVE_COLOR_FOCUS_ON_LIGHT,
  .on_dark = ADAPTIVE_COLOR_FOCUS_ON_DARK,
  .initial_threshold = 0.417738520,
  .switch_to_on_light_threshold = 0.46,
  .switch_to_on_dark_threshold = 0.38,
  .uniform = true,
};

struct adaptive_color_region {
  size_t x_begin;
  size_t x_end;
  size_t y_begin;
  size_t y_end;
};

static int compare_double(const void* lhs_pointer, const void* rhs_pointer) {
  double lhs = *(const double*)lhs_pointer;
  double rhs = *(const double*)rhs_pointer;
  return (lhs > rhs) - (lhs < rhs);
}

static void build_linear_srgb_lut(double lut[256]) {
  for (size_t value = 0; value < 256; ++value) {
    double component = (double)value / 255.0;
    lut[value] = component <= 0.04045
                 ? component / 12.92
                 : pow((component + 0.055) / 1.055, 2.4);
  }
}

static uint8_t unpremultiply(uint8_t component, uint8_t alpha) {
  unsigned int value = ((unsigned int)component * 255u
                        + (unsigned int)alpha / 2u)
                       / (unsigned int)alpha;
  return value > 255u ? UINT8_MAX : (uint8_t)value;
}

static int pixel_luminance(const uint8_t* pixel,
                           const double lut[256],
                           double* luminance) {
  uint8_t alpha = pixel[3];
  if (alpha < ADAPTIVE_COLOR_MIN_ALPHA) return 0;

  uint8_t blue = unpremultiply(pixel[0], alpha);
  uint8_t green = unpremultiply(pixel[1], alpha);
  uint8_t red = unpremultiply(pixel[2], alpha);
  *luminance = 0.2126 * lut[red]
               + 0.7152 * lut[green]
               + 0.0722 * lut[blue];
  return 1;
}

static size_t collect_region_luminance(
    const uint8_t* pixels,
    size_t bytes_per_row,
    struct adaptive_color_region region,
    const double lut[256],
    double samples[ADAPTIVE_COLOR_MAX_SIDE_SAMPLES]) {
  size_t sample_count = 0;
  for (size_t y = region.y_begin; y < region.y_end; ++y) {
    const uint8_t* row = pixels + y * bytes_per_row;
    for (size_t x = region.x_begin; x < region.x_end; ++x) {
      double luminance = 0.0;
      if (pixel_luminance(row + x * 4u, lut, &luminance)) {
        samples[sample_count++] = luminance;
      }
    }
  }
  return sample_count;
}

static double median_luminance(double* samples, size_t sample_count) {
  qsort(samples, sample_count, sizeof(samples[0]), compare_double);
  size_t middle = sample_count / 2u;
  if (sample_count % 2u != 0) return samples[middle];
  return (samples[middle - 1u] + samples[middle]) / 2.0;
}

static int adaptive_color_palette_is_valid(
    const struct adaptive_color_palette* palette) {
  return palette
         && palette->on_light != palette->on_dark
         && isfinite(palette->initial_threshold)
         && isfinite(palette->switch_to_on_light_threshold)
         && isfinite(palette->switch_to_on_dark_threshold)
         && palette->switch_to_on_dark_threshold >= 0.0
         && palette->switch_to_on_dark_threshold
            <= palette->initial_threshold
         && palette->initial_threshold
            <= palette->switch_to_on_light_threshold
         && palette->switch_to_on_light_threshold <= 1.0;
}

static int adaptive_color_is_palette_color(
    const struct adaptive_color_palette* palette,
    uint32_t color) {
  return color == palette->on_light || color == palette->on_dark;
}

static int adaptive_color_uniform_previous(
    const struct adaptive_color_palette* palette,
    const struct adaptive_color_cache* previous,
    uint32_t* color) {
  if (!previous
      || previous->valid_mask != ADAPTIVE_COLOR_ALL_SIDES_MASK
      || !adaptive_color_is_palette_color(palette, previous->colors[0])) {
    return 0;
  }
  for (size_t side = 1; side < ADAPTIVE_COLOR_SIDE_COUNT; ++side) {
    if (previous->colors[side] != previous->colors[0]) return 0;
  }
  if (color) *color = previous->colors[0];
  return 1;
}

enum adaptive_color_status adaptive_color_select(
    const struct adaptive_color_palette* palette,
    double luminance,
    int has_previous,
    uint32_t previous,
    uint32_t* color) {
  if (!adaptive_color_palette_is_valid(palette)
      || !color
      || !isfinite(luminance)
      || luminance < 0.0
      || luminance > 1.0) {
    return ADAPTIVE_COLOR_INVALID_ARGUMENT;
  }

  if (has_previous && previous == palette->on_dark) {
    *color = luminance >= palette->switch_to_on_light_threshold
             ? palette->on_light
             : palette->on_dark;
  } else if (has_previous && previous == palette->on_light) {
    *color = luminance <= palette->switch_to_on_dark_threshold
             ? palette->on_dark
             : palette->on_light;
  } else {
    *color = luminance >= palette->initial_threshold
             ? palette->on_light
             : palette->on_dark;
  }
  return ADAPTIVE_COLOR_OK;
}

static enum adaptive_color_status validate_layout(const uint8_t* pixels,
                                                   size_t pixels_size,
                                                   size_t width,
                                                   size_t height,
                                                   size_t bytes_per_row,
                                                   const uint32_t* fallback,
                                                   const void* result) {
  if (!pixels || !fallback || !result) return ADAPTIVE_COLOR_INVALID_ARGUMENT;
  if (width != ADAPTIVE_COLOR_IMAGE_WIDTH
      || height != ADAPTIVE_COLOR_IMAGE_HEIGHT) {
    return ADAPTIVE_COLOR_INVALID_DIMENSIONS;
  }
  if (width > SIZE_MAX / 4u) return ADAPTIVE_COLOR_SIZE_OVERFLOW;

  size_t row_bytes = width * 4u;
  if (bytes_per_row < row_bytes) return ADAPTIVE_COLOR_INVALID_STRIDE;
  if ((height - 1u) > (SIZE_MAX - row_bytes) / bytes_per_row) {
    return ADAPTIVE_COLOR_SIZE_OVERFLOW;
  }

  size_t required_size = (height - 1u) * bytes_per_row + row_bytes;
  if (pixels_size < required_size) return ADAPTIVE_COLOR_BUFFER_TOO_SMALL;
  return ADAPTIVE_COLOR_OK;
}

enum adaptive_color_status adaptive_color_analyze_bgra_with_palette(
    const uint8_t* pixels,
    size_t pixels_size,
    size_t width,
    size_t height,
    size_t bytes_per_row,
    const struct adaptive_color_palette* palette,
    const struct adaptive_color_cache* previous,
    const uint32_t fallback[ADAPTIVE_COLOR_SIDE_COUNT],
    struct adaptive_color_result* result) {
  enum adaptive_color_status status = validate_layout(pixels,
                                                       pixels_size,
                                                       width,
                                                       height,
                                                       bytes_per_row,
                                                       fallback,
                                                       result);
  if (status != ADAPTIVE_COLOR_OK) return status;
  if (!adaptive_color_palette_is_valid(palette)) {
    return ADAPTIVE_COLOR_INVALID_ARGUMENT;
  }

  uint32_t fallback_copy[ADAPTIVE_COLOR_SIDE_COUNT];
  memcpy(fallback_copy, fallback, sizeof(fallback_copy));

  struct adaptive_color_cache previous_copy = { 0 };
  if (previous) previous_copy = *previous;

  uint32_t uniform_previous_color = 0;
  int uniform_has_previous = 0;
  if (palette->uniform) {
    uniform_has_previous = adaptive_color_uniform_previous(
        palette,
        previous,
        &uniform_previous_color);
    previous_copy.valid_mask = uniform_has_previous
                               ? ADAPTIVE_COLOR_ALL_SIDES_MASK
                               : 0;
  } else {
    previous_copy.valid_mask &= ADAPTIVE_COLOR_ALL_SIDES_MASK;
    for (size_t side = 0; side < ADAPTIVE_COLOR_SIDE_COUNT; ++side) {
      uint8_t side_mask = ADAPTIVE_COLOR_SIDE_MASK(side);
      if ((previous_copy.valid_mask & side_mask)
          && !adaptive_color_is_palette_color(
              palette,
              previous_copy.colors[side])) {
        previous_copy.valid_mask &= (uint8_t)~side_mask;
      }
    }
  }

  struct adaptive_color_result next = { 0 };
  if (palette->uniform) {
    uint32_t initial_color = uniform_has_previous
                             ? uniform_previous_color
                             : fallback_copy[0];
    for (size_t side = 0; side < ADAPTIVE_COLOR_SIDE_COUNT; ++side) {
      next.colors[side] = initial_color;
    }
    next.cache_mask = uniform_has_previous
                      ? ADAPTIVE_COLOR_ALL_SIDES_MASK
                      : 0;
  } else {
    next.cache_mask = previous_copy.valid_mask;
    for (size_t side = 0; side < ADAPTIVE_COLOR_SIDE_COUNT; ++side) {
      uint8_t side_mask = ADAPTIVE_COLOR_SIDE_MASK(side);
      next.colors[side] = previous_copy.valid_mask & side_mask
                          ? previous_copy.colors[side]
                          : fallback_copy[side];
    }
  }

  const size_t endpoint = ADAPTIVE_COLOR_ENDPOINT_SKIP;
  const size_t outer = ADAPTIVE_COLOR_OUTER_SKIP;
  const size_t depth = ADAPTIVE_COLOR_INNER_DEPTH;
  const struct adaptive_color_region regions[ADAPTIVE_COLOR_SIDE_COUNT] = {
    [ADAPTIVE_COLOR_SIDE_TOP] = {
      endpoint, width - endpoint, outer, outer + depth,
    },
    [ADAPTIVE_COLOR_SIDE_RIGHT] = {
      width - outer - depth, width - outer, endpoint, height - endpoint,
    },
    [ADAPTIVE_COLOR_SIDE_BOTTOM] = {
      endpoint, width - endpoint, height - outer - depth, height - outer,
    },
    [ADAPTIVE_COLOR_SIDE_LEFT] = {
      outer, outer + depth, endpoint, height - endpoint,
    },
  };

  double lut[256];
  build_linear_srgb_lut(lut);

  double samples[ADAPTIVE_COLOR_MAX_SIDE_SAMPLES];
  double side_luminances[ADAPTIVE_COLOR_SIDE_COUNT];
  size_t sampled_side_count = 0;
  for (size_t side = 0; side < ADAPTIVE_COLOR_SIDE_COUNT; ++side) {
    struct adaptive_color_region region = regions[side];
    size_t region_size = (region.x_end - region.x_begin)
                         * (region.y_end - region.y_begin);
    size_t minimum_samples = (region_size + 3u) / 4u;
    size_t sample_count = collect_region_luminance(pixels,
                                                   bytes_per_row,
                                                   region,
                                                   lut,
                                                   samples);
    if (sample_count < minimum_samples) continue;

    uint8_t side_mask = ADAPTIVE_COLOR_SIDE_MASK(side);
    double luminance = median_luminance(samples, sample_count);
    next.luminance[side] = luminance;
    next.sampled_mask |= side_mask;
    if (palette->uniform) {
      side_luminances[sampled_side_count++] = luminance;
      continue;
    }

    uint32_t old_effective_color = next.colors[side];
    status = adaptive_color_select(
        palette,
        luminance,
        (previous_copy.valid_mask & side_mask) != 0,
        previous_copy.colors[side],
        &next.colors[side]);
    if (status != ADAPTIVE_COLOR_OK) return status;
    next.cache_mask |= side_mask;
    if (next.colors[side] != old_effective_color) {
      next.changed_mask |= side_mask;
    }
  }

  if (palette->uniform && sampled_side_count > 0) {
    double luminance = median_luminance(side_luminances, sampled_side_count);
    uint32_t color = 0;
    status = adaptive_color_select(palette,
                                   luminance,
                                   uniform_has_previous,
                                   uniform_previous_color,
                                   &color);
    if (status != ADAPTIVE_COLOR_OK) return status;
    for (size_t side = 0; side < ADAPTIVE_COLOR_SIDE_COUNT; ++side) {
      if (next.colors[side] != color) {
        next.changed_mask |= ADAPTIVE_COLOR_SIDE_MASK(side);
      }
      next.colors[side] = color;
    }
    next.cache_mask = ADAPTIVE_COLOR_ALL_SIDES_MASK;
  }

  *result = next;
  return ADAPTIVE_COLOR_OK;
}

enum adaptive_color_status adaptive_color_analyze_bgra(
    const uint8_t* pixels,
    size_t pixels_size,
    size_t width,
    size_t height,
    size_t bytes_per_row,
    const struct adaptive_color_cache* previous,
    const uint32_t fallback[ADAPTIVE_COLOR_SIDE_COUNT],
    struct adaptive_color_result* result) {
  return adaptive_color_analyze_bgra_with_palette(
      pixels,
      pixels_size,
      width,
      height,
      bytes_per_row,
      &ADAPTIVE_COLOR_PALETTE_BLACK_WHITE,
      previous,
      fallback,
      result);
}
