#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/adaptive_color.h"

#define TEST_PADDING 19u
#define TEST_STRIDE (ADAPTIVE_COLOR_IMAGE_WIDTH * 4u + TEST_PADDING)
#define TEST_BUFFER_SIZE (ADAPTIVE_COLOR_IMAGE_HEIGHT * TEST_STRIDE)
#define ALL_SIDES_MASK ((uint8_t)((1u << ADAPTIVE_COLOR_SIDE_COUNT) - 1u))

struct test_image {
  uint8_t pixels[TEST_BUFFER_SIZE];
};

static const uint32_t fallback_colors[ADAPTIVE_COLOR_SIDE_COUNT] = {
  UINT32_C(0xff102030),
  UINT32_C(0xff405060),
  UINT32_C(0xff708090),
  UINT32_C(0xffa0b0c0),
};

static uint8_t* test_pixel(struct test_image* image, size_t x, size_t y) {
  return image->pixels + y * TEST_STRIDE + x * 4u;
}

static void set_pixel(struct test_image* image,
                      size_t x,
                      size_t y,
                      uint8_t red,
                      uint8_t green,
                      uint8_t blue,
                      uint8_t alpha) {
  uint8_t* pixel = test_pixel(image, x, y);
  pixel[0] = blue;
  pixel[1] = green;
  pixel[2] = red;
  pixel[3] = alpha;
}

static void fill_image(struct test_image* image,
                       uint8_t red,
                       uint8_t green,
                       uint8_t blue,
                       uint8_t alpha) {
  memset(image, 0x5a, sizeof(*image));
  for (size_t y = 0; y < ADAPTIVE_COLOR_IMAGE_HEIGHT; ++y) {
    for (size_t x = 0; x < ADAPTIVE_COLOR_IMAGE_WIDTH; ++x) {
      set_pixel(image, x, y, red, green, blue, alpha);
    }
  }
}

static void fill_side(struct test_image* image,
                      enum adaptive_color_side side,
                      uint8_t value,
                      uint8_t alpha) {
  size_t x_begin = ADAPTIVE_COLOR_ENDPOINT_SKIP;
  size_t x_end = ADAPTIVE_COLOR_IMAGE_WIDTH - ADAPTIVE_COLOR_ENDPOINT_SKIP;
  size_t y_begin = ADAPTIVE_COLOR_OUTER_SKIP;
  size_t y_end = ADAPTIVE_COLOR_OUTER_SKIP + ADAPTIVE_COLOR_INNER_DEPTH;

  if (side == ADAPTIVE_COLOR_SIDE_BOTTOM) {
    y_begin = ADAPTIVE_COLOR_IMAGE_HEIGHT
              - ADAPTIVE_COLOR_OUTER_SKIP
              - ADAPTIVE_COLOR_INNER_DEPTH;
    y_end = ADAPTIVE_COLOR_IMAGE_HEIGHT - ADAPTIVE_COLOR_OUTER_SKIP;
  } else if (side == ADAPTIVE_COLOR_SIDE_LEFT
             || side == ADAPTIVE_COLOR_SIDE_RIGHT) {
    y_begin = ADAPTIVE_COLOR_ENDPOINT_SKIP;
    y_end = ADAPTIVE_COLOR_IMAGE_HEIGHT - ADAPTIVE_COLOR_ENDPOINT_SKIP;
    x_begin = side == ADAPTIVE_COLOR_SIDE_LEFT
              ? ADAPTIVE_COLOR_OUTER_SKIP
              : ADAPTIVE_COLOR_IMAGE_WIDTH
                - ADAPTIVE_COLOR_OUTER_SKIP
                - ADAPTIVE_COLOR_INNER_DEPTH;
    x_end = x_begin + ADAPTIVE_COLOR_INNER_DEPTH;
  }

  for (size_t y = y_begin; y < y_end; ++y) {
    for (size_t x = x_begin; x < x_end; ++x) {
      set_pixel(image, x, y, value, value, value, alpha);
    }
  }
}

static enum adaptive_color_status analyze(
    const struct test_image* image,
    const struct adaptive_color_cache* previous,
    struct adaptive_color_result* result) {
  return adaptive_color_analyze_bgra(image->pixels,
                                     sizeof(image->pixels),
                                     ADAPTIVE_COLOR_IMAGE_WIDTH,
                                     ADAPTIVE_COLOR_IMAGE_HEIGHT,
                                     TEST_STRIDE,
                                     previous,
                                     fallback_colors,
                                     result);
}

static enum adaptive_color_status analyze_with_palette(
    const struct test_image* image,
    const struct adaptive_color_palette* palette,
    const struct adaptive_color_cache* previous,
    struct adaptive_color_result* result) {
  return adaptive_color_analyze_bgra_with_palette(
      image->pixels,
      sizeof(image->pixels),
      ADAPTIVE_COLOR_IMAGE_WIDTH,
      ADAPTIVE_COLOR_IMAGE_HEIGHT,
      TEST_STRIDE,
      palette,
      previous,
      fallback_colors,
      result);
}

static void assert_all_colors(const struct adaptive_color_result* result,
                              uint32_t color) {
  for (size_t side = 0; side < ADAPTIVE_COLOR_SIDE_COUNT; ++side) {
    assert(result->colors[side] == color);
  }
}

static void test_builtin_palettes_and_focus_selector(void) {
  const struct adaptive_color_palette* black_white =
      &ADAPTIVE_COLOR_PALETTE_BLACK_WHITE;
  const struct adaptive_color_palette* focus = &ADAPTIVE_COLOR_PALETTE_FOCUS;
  uint32_t color = 0;

  assert(black_white->on_light == ADAPTIVE_COLOR_BLACK);
  assert(black_white->on_dark == ADAPTIVE_COLOR_WHITE);
  assert(black_white->initial_threshold == ADAPTIVE_COLOR_INITIAL_THRESHOLD);
  assert(focus->on_light == UINT32_C(0xffe5484d));
  assert(focus->on_dark == UINT32_C(0xffe1e3e4));
  assert(fabs(focus->initial_threshold - 0.417738520) < 0.000000001);
  assert(focus->switch_to_on_light_threshold == 0.46);
  assert(focus->switch_to_on_dark_threshold == 0.38);

  assert(adaptive_color_select(focus,
                               focus->initial_threshold - 0.000001,
                               0,
                               0,
                               &color) == ADAPTIVE_COLOR_OK);
  assert(color == focus->on_dark);
  assert(adaptive_color_select(focus,
                               focus->initial_threshold,
                               0,
                               0,
                               &color) == ADAPTIVE_COLOR_OK);
  assert(color == focus->on_light);

  assert(adaptive_color_select(focus, 0.459999, 1, focus->on_dark, &color)
         == ADAPTIVE_COLOR_OK);
  assert(color == focus->on_dark);
  assert(adaptive_color_select(focus, 0.46, 1, focus->on_dark, &color)
         == ADAPTIVE_COLOR_OK);
  assert(color == focus->on_light);
  assert(adaptive_color_select(focus, 0.380001, 1, focus->on_light, &color)
         == ADAPTIVE_COLOR_OK);
  assert(color == focus->on_light);
  assert(adaptive_color_select(focus, 0.38, 1, focus->on_light, &color)
         == ADAPTIVE_COLOR_OK);
  assert(color == focus->on_dark);

  /* A cached color from another palette must not select a hysteresis branch. */
  assert(adaptive_color_select(focus, 0.42, 1, ADAPTIVE_COLOR_BLACK, &color)
         == ADAPTIVE_COLOR_OK);
  assert(color == focus->on_light);
}

static void test_focus_palette_on_uniform_and_mixed_edges(void) {
  struct test_image image;
  struct adaptive_color_result result;
  const struct adaptive_color_palette* focus = &ADAPTIVE_COLOR_PALETTE_FOCUS;

  fill_image(&image, 0, 0, 0, 255);
  assert(analyze_with_palette(&image, focus, NULL, &result)
         == ADAPTIVE_COLOR_OK);
  assert_all_colors(&result, focus->on_dark);

  fill_image(&image, 255, 255, 255, 255);
  assert(analyze_with_palette(&image, focus, NULL, &result)
         == ADAPTIVE_COLOR_OK);
  assert_all_colors(&result, focus->on_light);

  fill_image(&image, 0, 0, 0, 0);
  fill_side(&image, ADAPTIVE_COLOR_SIDE_TOP, 0, 255);
  fill_side(&image, ADAPTIVE_COLOR_SIDE_RIGHT, 255, 255);
  fill_side(&image, ADAPTIVE_COLOR_SIDE_BOTTOM, 0, 255);
  fill_side(&image, ADAPTIVE_COLOR_SIDE_LEFT, 255, 255);
  assert(analyze_with_palette(&image, focus, NULL, &result)
         == ADAPTIVE_COLOR_OK);
  assert(result.sampled_mask == ALL_SIDES_MASK);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_TOP] == focus->on_dark);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_RIGHT] == focus->on_light);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_BOTTOM] == focus->on_dark);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_LEFT] == focus->on_light);
}

static void test_focus_palette_hysteresis_uses_previous_cache(void) {
  struct test_image image;
  struct adaptive_color_result result;
  const struct adaptive_color_palette* focus = &ADAPTIVE_COLOR_PALETTE_FOCUS;
  struct adaptive_color_cache previous = {
    .colors = {
      ADAPTIVE_COLOR_FOCUS_ON_DARK,
      ADAPTIVE_COLOR_FOCUS_ON_LIGHT,
      ADAPTIVE_COLOR_FOCUS_ON_DARK,
      ADAPTIVE_COLOR_FOCUS_ON_LIGHT,
    },
    .valid_mask = ALL_SIDES_MASK,
  };

  /* sRGB 170 has linear luminance inside the focus palette's 0.38-0.46 band. */
  fill_image(&image, 170, 170, 170, 255);
  assert(analyze_with_palette(&image, focus, &previous, &result)
         == ADAPTIVE_COLOR_OK);
  for (size_t side = 0; side < ADAPTIVE_COLOR_SIDE_COUNT; ++side) {
    assert(result.luminance[side] > focus->switch_to_on_dark_threshold);
    assert(result.luminance[side] < focus->switch_to_on_light_threshold);
    assert(result.colors[side] == previous.colors[side]);
  }
  assert(result.changed_mask == 0);

  fill_side(&image, ADAPTIVE_COLOR_SIDE_TOP, 181, 255);
  fill_side(&image, ADAPTIVE_COLOR_SIDE_RIGHT, 165, 255);
  assert(analyze_with_palette(&image, focus, &previous, &result)
         == ADAPTIVE_COLOR_OK);
  assert(result.luminance[ADAPTIVE_COLOR_SIDE_TOP]
         > focus->switch_to_on_light_threshold);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_TOP] == focus->on_light);
  assert(result.luminance[ADAPTIVE_COLOR_SIDE_RIGHT]
         < focus->switch_to_on_dark_threshold);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_RIGHT] == focus->on_dark);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_BOTTOM] == focus->on_dark);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_LEFT] == focus->on_light);
  assert(result.changed_mask
         == (ADAPTIVE_COLOR_SIDE_MASK(ADAPTIVE_COLOR_SIDE_TOP)
             | ADAPTIVE_COLOR_SIDE_MASK(ADAPTIVE_COLOR_SIDE_RIGHT)));
}

static void test_focus_palette_fallback_and_stale_cache_filtering(void) {
  struct test_image image;
  struct adaptive_color_result result;
  const struct adaptive_color_palette* focus = &ADAPTIVE_COLOR_PALETTE_FOCUS;
  fill_image(&image, 0, 0, 0, 0);

  struct adaptive_color_cache focus_previous = {
    .colors = {
      ADAPTIVE_COLOR_FOCUS_ON_LIGHT,
      ADAPTIVE_COLOR_FOCUS_ON_DARK,
      0,
      0,
    },
    .valid_mask = ADAPTIVE_COLOR_SIDE_MASK(ADAPTIVE_COLOR_SIDE_TOP)
                  | ADAPTIVE_COLOR_SIDE_MASK(ADAPTIVE_COLOR_SIDE_RIGHT),
  };
  assert(analyze_with_palette(&image, focus, &focus_previous, &result)
         == ADAPTIVE_COLOR_OK);
  assert(result.sampled_mask == 0);
  assert(result.cache_mask == focus_previous.valid_mask);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_TOP] == focus->on_light);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_RIGHT] == focus->on_dark);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_BOTTOM]
         == fallback_colors[ADAPTIVE_COLOR_SIDE_BOTTOM]);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_LEFT]
         == fallback_colors[ADAPTIVE_COLOR_SIDE_LEFT]);

  struct adaptive_color_cache black_white_previous = {
    .colors = {
      ADAPTIVE_COLOR_BLACK,
      ADAPTIVE_COLOR_WHITE,
      ADAPTIVE_COLOR_BLACK,
      ADAPTIVE_COLOR_WHITE,
    },
    .valid_mask = ALL_SIDES_MASK,
  };
  assert(analyze_with_palette(&image, focus, &black_white_previous, &result)
         == ADAPTIVE_COLOR_OK);
  assert(result.cache_mask == 0);
  for (size_t side = 0; side < ADAPTIVE_COLOR_SIDE_COUNT; ++side) {
    assert(result.colors[side] == fallback_colors[side]);
  }
}

static void test_black_and_white_images_choose_maximum_contrast(void) {
  struct test_image image;
  struct adaptive_color_result result;

  fill_image(&image, 0, 0, 0, 255);
  assert(analyze(&image, NULL, &result) == ADAPTIVE_COLOR_OK);
  assert(result.sampled_mask == ALL_SIDES_MASK);
  assert(result.cache_mask == ALL_SIDES_MASK);
  assert_all_colors(&result, ADAPTIVE_COLOR_WHITE);
  for (size_t side = 0; side < ADAPTIVE_COLOR_SIDE_COUNT; ++side) {
    assert(result.luminance[side] == 0.0);
  }

  fill_image(&image, 255, 255, 255, 255);
  assert(analyze(&image, NULL, &result) == ADAPTIVE_COLOR_OK);
  assert_all_colors(&result, ADAPTIVE_COLOR_BLACK);
  for (size_t side = 0; side < ADAPTIVE_COLOR_SIDE_COUNT; ++side) {
    assert(fabs(result.luminance[side] - 1.0) < 0.000001);
  }
}

static void test_sides_are_sampled_independently_in_drawing_order(void) {
  struct test_image image;
  struct adaptive_color_result result;
  fill_image(&image, 0, 0, 0, 0);
  fill_side(&image, ADAPTIVE_COLOR_SIDE_TOP, 0, 255);
  fill_side(&image, ADAPTIVE_COLOR_SIDE_RIGHT, 255, 255);
  fill_side(&image, ADAPTIVE_COLOR_SIDE_BOTTOM, 255, 255);
  fill_side(&image, ADAPTIVE_COLOR_SIDE_LEFT, 0, 255);

  assert(analyze(&image, NULL, &result) == ADAPTIVE_COLOR_OK);
  assert(result.sampled_mask == ALL_SIDES_MASK);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_TOP] == ADAPTIVE_COLOR_WHITE);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_RIGHT] == ADAPTIVE_COLOR_BLACK);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_BOTTOM] == ADAPTIVE_COLOR_BLACK);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_LEFT] == ADAPTIVE_COLOR_WHITE);
}

static void test_sampling_ignores_endpoints_outer_pixels_and_interior(void) {
  struct test_image image;
  struct adaptive_color_result result;
  fill_image(&image, 0, 0, 0, 255);

  /* Only the documented sample regions are white; all skipped pixels remain
   * black, including corners, the outer two pixels, and the interior. */
  fill_side(&image, ADAPTIVE_COLOR_SIDE_TOP, 255, 255);
  fill_side(&image, ADAPTIVE_COLOR_SIDE_RIGHT, 255, 255);
  fill_side(&image, ADAPTIVE_COLOR_SIDE_BOTTOM, 255, 255);
  fill_side(&image, ADAPTIVE_COLOR_SIDE_LEFT, 255, 255);

  assert(analyze(&image, NULL, &result) == ADAPTIVE_COLOR_OK);
  assert_all_colors(&result, ADAPTIVE_COLOR_BLACK);
}

static void test_alpha_filter_and_unpremultiplication(void) {
  struct test_image image;
  struct adaptive_color_result result;
  fill_image(&image, 0, 0, 0, 0);

  /* Premultiplied half-alpha white must be analyzed as white, not gray. */
  fill_side(&image, ADAPTIVE_COLOR_SIDE_TOP, 128, 128);
  /* Alpha 127 is ignored even when its color bytes are bright. */
  fill_side(&image, ADAPTIVE_COLOR_SIDE_RIGHT, 127, 127);

  assert(analyze(&image, NULL, &result) == ADAPTIVE_COLOR_OK);
  assert(result.sampled_mask == ADAPTIVE_COLOR_SIDE_MASK(
                                    ADAPTIVE_COLOR_SIDE_TOP));
  assert(result.colors[ADAPTIVE_COLOR_SIDE_TOP] == ADAPTIVE_COLOR_BLACK);
  assert(fabs(result.luminance[ADAPTIVE_COLOR_SIDE_TOP] - 1.0) < 0.000001);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_RIGHT]
         == fallback_colors[ADAPTIVE_COLOR_SIDE_RIGHT]);
}

static void test_exact_quarter_validity_threshold(void) {
  struct test_image image;
  struct adaptive_color_result result;
  fill_image(&image, 0, 0, 0, 0);

  const size_t required = ((ADAPTIVE_COLOR_IMAGE_WIDTH
                            - 2u * ADAPTIVE_COLOR_ENDPOINT_SKIP)
                           * ADAPTIVE_COLOR_INNER_DEPTH)
                          / 4u;
  const size_t sample_width = ADAPTIVE_COLOR_IMAGE_WIDTH
                              - 2u * ADAPTIVE_COLOR_ENDPOINT_SKIP;
  for (size_t index = 0; index < required - 1u; ++index) {
    size_t x = ADAPTIVE_COLOR_ENDPOINT_SKIP + index % sample_width;
    size_t y = ADAPTIVE_COLOR_OUTER_SKIP + index / sample_width;
    set_pixel(&image, x, y, 0, 0, 0, 255);
  }
  assert(analyze(&image, NULL, &result) == ADAPTIVE_COLOR_OK);
  assert((result.sampled_mask
          & ADAPTIVE_COLOR_SIDE_MASK(ADAPTIVE_COLOR_SIDE_TOP)) == 0);

  size_t final_index = required - 1u;
  set_pixel(&image,
            ADAPTIVE_COLOR_ENDPOINT_SKIP + final_index % sample_width,
            ADAPTIVE_COLOR_OUTER_SKIP + final_index / sample_width,
            0, 0, 0, 255);
  assert(analyze(&image, NULL, &result) == ADAPTIVE_COLOR_OK);
  assert(result.sampled_mask
         & ADAPTIVE_COLOR_SIDE_MASK(ADAPTIVE_COLOR_SIDE_TOP));
}

static void test_hysteresis_uses_only_prior_adaptive_colors(void) {
  struct test_image image;
  struct adaptive_color_result result;
  struct adaptive_color_cache previous = {
    .colors = {
      ADAPTIVE_COLOR_WHITE,
      ADAPTIVE_COLOR_BLACK,
      ADAPTIVE_COLOR_WHITE,
      ADAPTIVE_COLOR_BLACK,
    },
    .valid_mask = ALL_SIDES_MASK,
  };

  /* sRGB 120 has linear luminance between 0.16 and 0.20. */
  fill_image(&image, 120, 120, 120, 255);
  assert(analyze(&image, &previous, &result) == ADAPTIVE_COLOR_OK);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_TOP] == ADAPTIVE_COLOR_WHITE);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_RIGHT] == ADAPTIVE_COLOR_BLACK);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_BOTTOM] == ADAPTIVE_COLOR_WHITE);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_LEFT] == ADAPTIVE_COLOR_BLACK);
  assert(result.changed_mask == 0);

  fill_side(&image, ADAPTIVE_COLOR_SIDE_TOP, 130, 255);
  fill_side(&image, ADAPTIVE_COLOR_SIDE_RIGHT, 100, 255);
  assert(analyze(&image, &previous, &result) == ADAPTIVE_COLOR_OK);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_TOP] == ADAPTIVE_COLOR_BLACK);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_RIGHT] == ADAPTIVE_COLOR_WHITE);
  assert(result.changed_mask
         & ADAPTIVE_COLOR_SIDE_MASK(ADAPTIVE_COLOR_SIDE_TOP));
  assert(result.changed_mask
         & ADAPTIVE_COLOR_SIDE_MASK(ADAPTIVE_COLOR_SIDE_RIGHT));

  /* With no adaptive cache, the 0.179 initial threshold selects black. */
  assert(analyze(&image, NULL, &result) == ADAPTIVE_COLOR_OK);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_BOTTOM] == ADAPTIVE_COLOR_BLACK);
}

static void test_invalid_sides_retain_cache_then_fallback(void) {
  struct test_image image;
  struct adaptive_color_result result;
  struct adaptive_color_cache previous = {
    .colors = { ADAPTIVE_COLOR_WHITE, ADAPTIVE_COLOR_BLACK, 0, 0 },
    .valid_mask = ADAPTIVE_COLOR_SIDE_MASK(ADAPTIVE_COLOR_SIDE_TOP)
                  | ADAPTIVE_COLOR_SIDE_MASK(ADAPTIVE_COLOR_SIDE_RIGHT)
                  | UINT8_C(0xf0),
  };
  fill_image(&image, 0, 0, 0, 0);

  assert(analyze(&image, &previous, &result) == ADAPTIVE_COLOR_OK);
  assert(result.sampled_mask == 0);
  assert(result.cache_mask
         == (ADAPTIVE_COLOR_SIDE_MASK(ADAPTIVE_COLOR_SIDE_TOP)
             | ADAPTIVE_COLOR_SIDE_MASK(ADAPTIVE_COLOR_SIDE_RIGHT)));
  assert(result.colors[ADAPTIVE_COLOR_SIDE_TOP] == ADAPTIVE_COLOR_WHITE);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_RIGHT] == ADAPTIVE_COLOR_BLACK);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_BOTTOM]
         == fallback_colors[ADAPTIVE_COLOR_SIDE_BOTTOM]);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_LEFT]
         == fallback_colors[ADAPTIVE_COLOR_SIDE_LEFT]);
  assert(result.changed_mask == 0);
}

static void test_median_is_used_instead_of_mean(void) {
  struct test_image image;
  struct adaptive_color_result result;
  fill_image(&image, 0, 0, 0, 0);
  fill_side(&image, ADAPTIVE_COLOR_SIDE_TOP, 0, 255);

  /* A minority of white outliers should not change a black median. */
  size_t changed = 0;
  for (size_t y = ADAPTIVE_COLOR_OUTER_SKIP;
       y < ADAPTIVE_COLOR_OUTER_SKIP + ADAPTIVE_COLOR_INNER_DEPTH;
       ++y) {
    for (size_t x = ADAPTIVE_COLOR_ENDPOINT_SKIP;
         x < ADAPTIVE_COLOR_IMAGE_WIDTH - ADAPTIVE_COLOR_ENDPOINT_SKIP;
         ++x) {
      if (changed++ >= 300u) break;
      set_pixel(&image, x, y, 255, 255, 255, 255);
    }
    if (changed >= 300u) break;
  }

  assert(analyze(&image, NULL, &result) == ADAPTIVE_COLOR_OK);
  assert(result.luminance[ADAPTIVE_COLOR_SIDE_TOP] == 0.0);
  assert(result.colors[ADAPTIVE_COLOR_SIDE_TOP] == ADAPTIVE_COLOR_WHITE);
}

static void test_palette_validation_and_result_atomicity(void) {
  const struct adaptive_color_palette* focus = &ADAPTIVE_COLOR_PALETTE_FOCUS;
  struct adaptive_color_palette invalid = *focus;
  uint32_t color = UINT32_C(0x12345678);

  assert(adaptive_color_select(NULL, 0.5, 0, 0, &color)
         == ADAPTIVE_COLOR_INVALID_ARGUMENT);
  assert(color == UINT32_C(0x12345678));
  assert(adaptive_color_select(focus, NAN, 0, 0, &color)
         == ADAPTIVE_COLOR_INVALID_ARGUMENT);
  assert(adaptive_color_select(focus, -0.001, 0, 0, &color)
         == ADAPTIVE_COLOR_INVALID_ARGUMENT);
  assert(adaptive_color_select(focus, 1.001, 0, 0, &color)
         == ADAPTIVE_COLOR_INVALID_ARGUMENT);
  assert(adaptive_color_select(focus, 0.5, 0, 0, NULL)
         == ADAPTIVE_COLOR_INVALID_ARGUMENT);

  invalid.on_dark = invalid.on_light;
  assert(adaptive_color_select(&invalid, 0.5, 0, 0, &color)
         == ADAPTIVE_COLOR_INVALID_ARGUMENT);
  invalid = *focus;
  invalid.initial_threshold = INFINITY;
  assert(adaptive_color_select(&invalid, 0.5, 0, 0, &color)
         == ADAPTIVE_COLOR_INVALID_ARGUMENT);
  invalid = *focus;
  invalid.switch_to_on_dark_threshold = -0.01;
  assert(adaptive_color_select(&invalid, 0.5, 0, 0, &color)
         == ADAPTIVE_COLOR_INVALID_ARGUMENT);
  invalid = *focus;
  invalid.switch_to_on_dark_threshold = invalid.initial_threshold + 0.01;
  assert(adaptive_color_select(&invalid, 0.5, 0, 0, &color)
         == ADAPTIVE_COLOR_INVALID_ARGUMENT);
  invalid = *focus;
  invalid.switch_to_on_light_threshold = invalid.initial_threshold - 0.01;
  assert(adaptive_color_select(&invalid, 0.5, 0, 0, &color)
         == ADAPTIVE_COLOR_INVALID_ARGUMENT);
  invalid = *focus;
  invalid.switch_to_on_light_threshold = 1.01;
  assert(adaptive_color_select(&invalid, 0.5, 0, 0, &color)
         == ADAPTIVE_COLOR_INVALID_ARGUMENT);

  struct test_image image;
  fill_image(&image, 255, 255, 255, 255);
  struct adaptive_color_result sentinel;
  memset(&sentinel, 0xa5, sizeof(sentinel));
  struct adaptive_color_result result = sentinel;
  invalid = *focus;
  invalid.on_dark = invalid.on_light;
  assert(adaptive_color_analyze_bgra_with_palette(
             image.pixels,
             sizeof(image.pixels),
             ADAPTIVE_COLOR_IMAGE_WIDTH,
             ADAPTIVE_COLOR_IMAGE_HEIGHT,
             TEST_STRIDE,
             &invalid,
             NULL,
             fallback_colors,
             &result) == ADAPTIVE_COLOR_INVALID_ARGUMENT);
  assert(memcmp(&result, &sentinel, sizeof(result)) == 0);
}

static void test_layout_validation_and_result_atomicity(void) {
  struct test_image image;
  fill_image(&image, 0, 0, 0, 255);
  struct adaptive_color_result sentinel;
  memset(&sentinel, 0xa5, sizeof(sentinel));
  struct adaptive_color_result result;

#define EXPECT_ERROR(expression, expected) do { \
    result = sentinel; \
    assert((expression) == (expected)); \
    assert(memcmp(&result, &sentinel, sizeof(result)) == 0); \
  } while (0)

  EXPECT_ERROR(adaptive_color_analyze_bgra(NULL,
                                           sizeof(image.pixels),
                                           ADAPTIVE_COLOR_IMAGE_WIDTH,
                                           ADAPTIVE_COLOR_IMAGE_HEIGHT,
                                           TEST_STRIDE,
                                           NULL,
                                           fallback_colors,
                                           &result),
               ADAPTIVE_COLOR_INVALID_ARGUMENT);
  EXPECT_ERROR(adaptive_color_analyze_bgra(image.pixels,
                                           sizeof(image.pixels),
                                           127,
                                           ADAPTIVE_COLOR_IMAGE_HEIGHT,
                                           TEST_STRIDE,
                                           NULL,
                                           fallback_colors,
                                           &result),
               ADAPTIVE_COLOR_INVALID_DIMENSIONS);
  EXPECT_ERROR(adaptive_color_analyze_bgra(image.pixels,
                                           sizeof(image.pixels),
                                           ADAPTIVE_COLOR_IMAGE_WIDTH,
                                           ADAPTIVE_COLOR_IMAGE_HEIGHT,
                                           ADAPTIVE_COLOR_IMAGE_WIDTH * 4u - 1u,
                                           NULL,
                                           fallback_colors,
                                           &result),
               ADAPTIVE_COLOR_INVALID_STRIDE);
  EXPECT_ERROR(adaptive_color_analyze_bgra(image.pixels,
                                           1,
                                           ADAPTIVE_COLOR_IMAGE_WIDTH,
                                           ADAPTIVE_COLOR_IMAGE_HEIGHT,
                                           TEST_STRIDE,
                                           NULL,
                                           fallback_colors,
                                           &result),
               ADAPTIVE_COLOR_BUFFER_TOO_SMALL);
  EXPECT_ERROR(adaptive_color_analyze_bgra(image.pixels,
                                           SIZE_MAX,
                                           ADAPTIVE_COLOR_IMAGE_WIDTH,
                                           ADAPTIVE_COLOR_IMAGE_HEIGHT,
                                           SIZE_MAX,
                                           NULL,
                                           fallback_colors,
                                           &result),
               ADAPTIVE_COLOR_SIZE_OVERFLOW);

#undef EXPECT_ERROR
}

static void test_fallback_may_alias_result_storage(void) {
  struct test_image image;
  struct adaptive_color_result result = {
    .colors = {
      UINT32_C(0xff010203),
      UINT32_C(0xff040506),
      UINT32_C(0xff070809),
      UINT32_C(0xff0a0b0c),
    },
  };
  uint32_t expected[ADAPTIVE_COLOR_SIDE_COUNT];
  memcpy(expected, result.colors, sizeof(expected));
  fill_image(&image, 0, 0, 0, 0);

  assert(adaptive_color_analyze_bgra(image.pixels,
                                     sizeof(image.pixels),
                                     ADAPTIVE_COLOR_IMAGE_WIDTH,
                                     ADAPTIVE_COLOR_IMAGE_HEIGHT,
                                     TEST_STRIDE,
                                     NULL,
                                     result.colors,
                                     &result) == ADAPTIVE_COLOR_OK);
  assert(memcmp(result.colors, expected, sizeof(expected)) == 0);
}

int main(void) {
  test_builtin_palettes_and_focus_selector();
  test_focus_palette_on_uniform_and_mixed_edges();
  test_focus_palette_hysteresis_uses_previous_cache();
  test_focus_palette_fallback_and_stale_cache_filtering();
  test_black_and_white_images_choose_maximum_contrast();
  test_sides_are_sampled_independently_in_drawing_order();
  test_sampling_ignores_endpoints_outer_pixels_and_interior();
  test_alpha_filter_and_unpremultiplication();
  test_exact_quarter_validity_threshold();
  test_hysteresis_uses_only_prior_adaptive_colors();
  test_invalid_sides_retain_cache_then_fallback();
  test_median_is_used_instead_of_mean();
  test_palette_validation_and_result_atomicity();
  test_layout_validation_and_result_atomicity();
  test_fallback_may_alias_result_storage();
  puts("adaptive color tests passed");
  return 0;
}
