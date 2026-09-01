#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../src/parse.c"

static void assert_close(float actual, float expected) {
  assert(fabsf(actual - expected) < 0.0001f);
}

int main(void) {
  struct settings settings = {};
  char glow_gradient[] =
      "active_color=glow(gradient(top_left=0xffff0000,bottom_right=0xff0000ff))";
  char* glow_gradient_arguments[] = { glow_gradient };

  uint32_t mask = parse_settings(&settings, 1, glow_gradient_arguments);
  assert(mask == BORDER_UPDATE_MASK_ACTIVE);
  assert(settings.active_window.stype == COLOR_STYLE_GRADIENT);
  assert(settings.active_window.glow);
  assert(settings.active_window.gradient.direction == TL_TO_BR);
  assert(settings.active_window.gradient.color1 == 0xffff0000);
  assert(settings.active_window.gradient.color2 == 0xff0000ff);

  char reverse_glow_gradient[] =
      "active_color=glow(gradient(top_right=0xff00ff00,bottom_left=0xffffffff))";
  char* reverse_glow_gradient_arguments[] = { reverse_glow_gradient };
  mask = parse_settings(&settings, 1, reverse_glow_gradient_arguments);
  assert(mask == BORDER_UPDATE_MASK_ACTIVE);
  assert(settings.active_window.glow);
  assert(settings.active_window.gradient.direction == TR_TO_BL);

  char plain_gradient[] =
      "active_color=gradient(top_left=0xff010203,bottom_right=0xff040506)";
  char* plain_gradient_arguments[] = { plain_gradient };
  mask = parse_settings(&settings, 1, plain_gradient_arguments);
  assert(mask == BORDER_UPDATE_MASK_ACTIVE);
  assert(settings.active_window.stype == COLOR_STYLE_GRADIENT);
  assert(!settings.active_window.glow);
  assert(settings.active_window.gradient.direction == TL_TO_BR);

  char solid_glow[] = "active_color=glow(0xff123456)";
  char* solid_glow_arguments[] = { solid_glow };
  mask = parse_settings(&settings, 1, solid_glow_arguments);
  assert(mask == BORDER_UPDATE_MASK_ACTIVE);
  assert(settings.active_window.stype == COLOR_STYLE_SOLID);
  assert(settings.active_window.glow);
  for (int side = 0; side < BORDER_SIDE_COUNT; side++) {
    assert(settings.active_window.colors[side] == 0xff123456);
  }

  char plain_solid[] = "active_color=0xffabcdef";
  char* plain_solid_arguments[] = { plain_solid };
  mask = parse_settings(&settings, 1, plain_solid_arguments);
  assert(mask == BORDER_UPDATE_MASK_ACTIVE);
  assert(settings.active_window.stype == COLOR_STYLE_SOLID);
  assert(!settings.active_window.glow);
  for (int side = 0; side < BORDER_SIDE_COUNT; side++) {
    assert(settings.active_window.colors[side] == 0xffabcdef);
  }

  char two_sides[] = "active_color=0xffff0000, 0xff0000ff";
  char* two_sides_arguments[] = { two_sides };
  mask = parse_settings(&settings, 1, two_sides_arguments);
  assert(mask == BORDER_UPDATE_MASK_ACTIVE);
  assert(settings.active_window.colors[BORDER_SIDE_TOP] == 0xffff0000);
  assert(settings.active_window.colors[BORDER_SIDE_RIGHT] == 0);
  assert(settings.active_window.colors[BORDER_SIDE_BOTTOM] == 0xff0000ff);
  assert(settings.active_window.colors[BORDER_SIDE_LEFT] == 0);

  char four_sides[] =
      "inactive_color=glow(0xff000001,0xff000002,0xff000003,0xff000004)";
  char* four_sides_arguments[] = { four_sides };
  mask = parse_settings(&settings, 1, four_sides_arguments);
  assert(mask == BORDER_UPDATE_MASK_INACTIVE);
  assert(settings.inactive_window.glow);
  for (int side = 0; side < BORDER_SIDE_COUNT; side++) {
    assert(settings.inactive_window.colors[side] == 0xff000001u + side);
  }

  struct color_style previous_background = settings.background;
  char invalid_background[] =
      "background_color=0xff000001,0xff000002";
  char* invalid_background_arguments[] = { invalid_background };
  mask = parse_settings(&settings, 1, invalid_background_arguments);
  assert(mask == 0);
  assert(memcmp(&settings.background,
                &previous_background,
                sizeof(struct color_style)) == 0);

  struct color_style previous_inactive = settings.inactive_window;
  char invalid_three_sides[] =
      "inactive_color=0xff000001,0xff000002,0xff000003";
  char* invalid_three_sides_arguments[] = { invalid_three_sides };
  mask = parse_settings(&settings, 1, invalid_three_sides_arguments);
  assert(mask == 0);
  assert(memcmp(&settings.inactive_window,
                &previous_inactive,
                sizeof(struct color_style)) == 0);

  char invalid_long_hex[] = "inactive_color=0x100000000";
  char* invalid_long_hex_arguments[] = { invalid_long_hex };
  mask = parse_settings(&settings, 1, invalid_long_hex_arguments);
  assert(mask == 0);
  assert(memcmp(&settings.inactive_window,
                &previous_inactive,
                sizeof(struct color_style)) == 0);

  struct color_style previous_style = settings.active_window;
  char invalid[] = "active_color=glow(0xff123456)trailing";
  char* invalid_arguments[] = { invalid };
  mask = parse_settings(&settings, 1, invalid_arguments);
  assert(mask == 0);
  assert(memcmp(&settings.active_window,
                &previous_style,
                sizeof(struct color_style)) == 0);

  settings.border_style = BORDER_STYLE_ROUND;
  char style_none[] = "style=none";
  char* style_none_arguments[] = { style_none };
  mask = parse_settings(&settings, 1, style_none_arguments);
  assert(mask == BORDER_UPDATE_MASK_RECREATE_ALL);
  assert(settings.border_style == BORDER_STYLE_NONE);

  char style_round[] = "style=round";
  char* style_round_arguments[] = { style_round };
  mask = parse_settings(&settings, 1, style_round_arguments);
  assert(mask == BORDER_UPDATE_MASK_RECREATE_ALL);
  assert(settings.border_style == BORDER_STYLE_ROUND);

  char style_uniform[] = "style=u";
  char* style_uniform_arguments[] = { style_uniform };
  mask = parse_settings(&settings, 1, style_uniform_arguments);
  assert(mask == BORDER_UPDATE_MASK_ALL);
  assert(settings.border_style == BORDER_STYLE_ROUND_UNIFORM);

  char invalid_style[] = "style=banana";
  char* invalid_style_arguments[] = { invalid_style };
  mask = parse_settings(&settings, 1, invalid_style_arguments);
  assert(mask == 0);
  assert(settings.border_style == BORDER_STYLE_ROUND_UNIFORM);

  float a, r, g, b;
  colors_mix(0xffff0000, 0xff0000ff, &a, &r, &g, &b);
  assert_close(a, 1.0f);
  assert_close(r, 0.5f);
  assert_close(g, 0.0f);
  assert_close(b, 0.5f);

  colors_mix(0x00ff0000, 0xff0000ff, &a, &r, &g, &b);
  assert_close(a, 0.5f);
  assert_close(r, 0.0f);
  assert_close(g, 0.0f);
  assert_close(b, 1.0f);

  puts("color style parsing and glow mixing: ok");
  return 0;
}
