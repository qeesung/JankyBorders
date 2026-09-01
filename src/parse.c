#include "parse.h"
#include "active_only.h"
#include "border.h"
#include "hashtable.h"
#include <stdlib.h>

static bool str_starts_with(char* string, char* prefix) {
  if (!string || !prefix) return false;
  if (strlen(string) < strlen(prefix)) return false;
  if (strncmp(prefix, string, strlen(prefix)) == 0) return true;
  return false;
}

static bool parse_list(struct table* list, char* token, bool* enabled) {
  if (!list || !token || !enabled) return false;

  size_t token_len = strlen(token);
  if (token_len == SIZE_MAX) return false;

  char* copy = malloc(token_len + 1);
  if (!copy) return false;
  memcpy(copy, token, token_len + 1);

  struct table parsed = { 0 };
  int capacity = list->capacity > 0 ? list->capacity : 64;
  if (!table_init(&parsed, capacity, list->hash, list->cmp)) {
    free(copy);
    return false;
  }

  char* name;
  char* cursor = copy;
  bool entry_found = false;

  while((name = strsep(&cursor, ","))) {
    if (strlen(name) > 0) {
      if (!_table_add(&parsed, name, strlen(name) + 1, (void*)true)) {
        table_free(&parsed);
        free(copy);
        return false;
      }
      entry_found = true;
    }
  }

  struct table previous = *list;
  *list = parsed;
  table_free(&previous);
  free(copy);
  *enabled = entry_found;
  return true;
}

static bool parse_gradient(struct color_style* style,
                           const char* token,
                           const char* format,
                           int direction,
                           bool glow) {
  uint32_t color1;
  uint32_t color2;
  int consumed = 0;
  if (sscanf(token, format, &color1, &color2, &consumed) != 2
      || consumed != (int)strlen(token)) {
    return false;
  }

  style->stype = COLOR_STYLE_GRADIENT;
  style->glow = glow;
  style->gradient.direction = direction;
  style->gradient.color1 = color1;
  style->gradient.color2 = color2;
  return true;
}

static int hex_digit_value(char digit) {
  if (digit >= '0' && digit <= '9') return digit - '0';
  if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
  if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
  return -1;
}

static void skip_inline_space(const char** cursor) {
  while (**cursor == ' ' || **cursor == '\t') (*cursor)++;
}

static bool parse_hex_color(const char** cursor, uint32_t* color) {
  if ((*cursor)[0] != '0' || (*cursor)[1] != 'x') return false;
  *cursor += 2;

  uint32_t value = 0;
  int digits = 0;
  int digit;
  while ((digit = hex_digit_value(**cursor)) >= 0) {
    if (digits == 8) return false;
    value = (value << 4) | (uint32_t)digit;
    digits++;
    (*cursor)++;
  }
  if (!digits) return false;

  *color = value;
  return true;
}

static bool parse_color_list(uint32_t colors[BORDER_SIDE_COUNT],
                             const char* token,
                             char terminator,
                             bool allow_per_edge) {
  uint32_t parsed[BORDER_SIDE_COUNT] = {0};
  int count = 0;
  const char* cursor = token;

  while (count < BORDER_SIDE_COUNT) {
    skip_inline_space(&cursor);
    if (!parse_hex_color(&cursor, &parsed[count])) return false;
    count++;
    skip_inline_space(&cursor);
    if (*cursor != ',') break;
    cursor++;
  }

  skip_inline_space(&cursor);
  if (terminator) {
    if (*cursor != terminator || cursor[1] != '\0') return false;
  } else if (*cursor != '\0') {
    return false;
  }

  if (!allow_per_edge && count != 1) return false;
  if (count == 1) {
    for (int side = 0; side < BORDER_SIDE_COUNT; side++) {
      colors[side] = parsed[0];
    }
  } else if (count == 2) {
    colors[BORDER_SIDE_TOP] = parsed[0];
    colors[BORDER_SIDE_RIGHT] = 0;
    colors[BORDER_SIDE_BOTTOM] = parsed[1];
    colors[BORDER_SIDE_LEFT] = 0;
  } else if (count == BORDER_SIDE_COUNT) {
    memcpy(colors, parsed, sizeof(parsed));
  } else {
    return false;
  }
  return true;
}

static bool parse_solid(struct color_style* style,
                        const char* token,
                        bool glow,
                        bool allow_per_edge) {
  const char* prefix = glow ? "=glow(" : "=";
  size_t prefix_length = strlen(prefix);
  if (strncmp(token, prefix, prefix_length) != 0) return false;

  uint32_t colors[BORDER_SIDE_COUNT];
  if (!parse_color_list(colors,
                        token + prefix_length,
                        glow ? ')' : '\0',
                        allow_per_edge)) {
    return false;
  }

  style->stype = COLOR_STYLE_SOLID;
  style->glow = glow;
  memcpy(style->colors, colors, sizeof(colors));
  return true;
}

static bool parse_color(struct color_style* style,
                        const char* token,
                        bool allow_per_edge) {
  if (parse_gradient(style,
                     token,
                     "=glow(gradient(top_left=0x%8x,bottom_right=0x%8x))%n",
                     TL_TO_BR,
                     true)
      || parse_gradient(style,
                        token,
                        "=glow(gradient(top_right=0x%8x,bottom_left=0x%8x))%n",
                        TR_TO_BL,
                        true)
      || parse_gradient(style,
                        token,
                        "=gradient(top_left=0x%8x,bottom_right=0x%8x)%n",
                        TL_TO_BR,
                        false)
      || parse_gradient(style,
                        token,
                        "=gradient(top_right=0x%8x,bottom_left=0x%8x)%n",
                        TR_TO_BL,
                        false)
      || parse_solid(style, token, true, allow_per_edge)
      || parse_solid(style, token, false, allow_per_edge)) {
    return true;
  }

  printf("[?] Borders: Invalid color argument color%s\n", token);

  return false;
}

static bool parse_border_style(const char* argument, char* style) {
  static const struct {
    const char* name;
    char value;
  } styles[] = {
    { "round", BORDER_STYLE_ROUND },
    { "r", BORDER_STYLE_ROUND },
    { "uniform", BORDER_STYLE_ROUND_UNIFORM },
    { "u", BORDER_STYLE_ROUND_UNIFORM },
    { "square", BORDER_STYLE_SQUARE },
    { "s", BORDER_STYLE_SQUARE },
    { "none", BORDER_STYLE_NONE },
    { "n", BORDER_STYLE_NONE }
  };

  if (strncmp(argument, "style=", 6) != 0) return false;
  const char* value = argument + 6;
  for (size_t i = 0; i < sizeof(styles) / sizeof(styles[0]); i++) {
    if (strcmp(value, styles[i].name) == 0) {
      *style = styles[i].value;
      return true;
    }
  }
  return false;
}

static uint32_t parse_settings_internal(struct settings* settings,
                                        int count,
                                        char** arguments,
                                        bool global_controls) {
  if (!settings || count <= 0 || !arguments) return 0;
  static char active_color[] = "active_color";
  static char inactive_color[] = "inactive_color";
  static char background_color[] = "background_color";
  static char blacklist[] = "blacklist=";
  static char whitelist[] = "whitelist=";

  char order = 'a';
  uint32_t update_mask = 0;
  for (int i = 0; i < count; i++) {
    if (!arguments[i]) continue;
    if (str_starts_with(arguments[i], active_color)) {
      if (parse_color(&settings->active_window,
                      arguments[i] + strlen(active_color),
                      true                                )) {
        update_mask |= BORDER_UPDATE_MASK_ACTIVE;
      }
    }
    else  if (str_starts_with(arguments[i], inactive_color)) {
      if (parse_color(&settings->inactive_window,
                      arguments[i] + strlen(inactive_color),
                      true                                  )) {
        update_mask |= BORDER_UPDATE_MASK_INACTIVE;
      }
    }
    else if (str_starts_with(arguments[i], background_color)) {
      struct color_style bg;
      if (parse_color(&bg,
                      arguments[i] + strlen(background_color),
                      false                                  )) {
        if (bg.stype == COLOR_STYLE_GRADIENT) {
          printf("[?] Borders: background_color does not support gradients\n");
        } else {
          settings->background = bg;
          update_mask |= BORDER_UPDATE_MASK_ALL;
          settings->show_background =
              settings->background.colors[0] & 0xff000000;
        }
      }
    }
    else if (str_starts_with(arguments[i], blacklist)) {
      if (global_controls) {
        bool enabled = false;
        if (parse_list(&g_blacklist,
                       arguments[i] + strlen(blacklist),
                       &enabled)) {
          g_blacklist_enabled = enabled;
          update_mask |= BORDER_UPDATE_MASK_RECREATE_ALL;
        } else {
          printf("[!] Borders: Failed to update blacklist\n");
        }
      }
    }
    else if (str_starts_with(arguments[i], whitelist)) {
      if (global_controls) {
        bool enabled = false;
        if (parse_list(&g_whitelist,
                       arguments[i] + strlen(whitelist),
                       &enabled)) {
          g_whitelist_enabled = enabled;
          update_mask |= BORDER_UPDATE_MASK_RECREATE_ALL;
        } else {
          printf("[!] Borders: Failed to update whitelist\n");
        }
      }
    }
    else if (sscanf(arguments[i], "width=%f", &settings->border_width) == 1) {
      update_mask |= BORDER_UPDATE_MASK_ALL;
    }
    else if (sscanf(arguments[i], "order=%c", &order) == 1) {
      if (order == 'a') settings->border_order = BORDER_ORDER_ABOVE;
      else settings->border_order = BORDER_ORDER_BELOW;
      update_mask |= BORDER_UPDATE_MASK_ALL;
    }
    else if (str_starts_with(arguments[i], "style=")) {
      char style;
      if (parse_border_style(arguments[i], &style)) {
        bool recreate = settings->border_style == BORDER_STYLE_NONE
                        || style == BORDER_STYLE_NONE;
        settings->border_style = style;
        update_mask |= recreate
                       ? BORDER_UPDATE_MASK_RECREATE_ALL
                       : BORDER_UPDATE_MASK_ALL;
      } else {
        printf("[?] Borders: Invalid argument '%s'\n", arguments[i]);
      }
    }
    else if (strcmp(arguments[i], "hidpi=on") == 0) {
      update_mask |= BORDER_UPDATE_MASK_RECREATE_ALL;
      settings->hidpi = true;
    }
    else if (strcmp(arguments[i], "hidpi=off") == 0) {
      update_mask |= BORDER_UPDATE_MASK_RECREATE_ALL;
      settings->hidpi = false;
    }
    else if (strcmp(arguments[i], "ax_focus=on") == 0) {
      settings->ax_focus = true;
      update_mask |= BORDER_UPDATE_MASK_SETTING;
    }
    else if (strcmp(arguments[i], "ax_focus=off") == 0) {
      settings->ax_focus = false;
      update_mask |= BORDER_UPDATE_MASK_SETTING;
    }
    else if (active_only_parse_argument(arguments[i],
                                        &settings->active_only)) {
      if (global_controls) update_mask |= BORDER_UPDATE_MASK_RECREATE_ALL;
    }
    else if (str_starts_with(arguments[i], "apply-to=")) {
      if (global_controls
          && sscanf(arguments[i], "apply-to=%u", &settings->apply_to) == 1) {
        update_mask |= BORDER_UPDATE_MASK_SETTING;
      }
    }
    else {
      printf("[?] Borders: Invalid argument '%s'\n", arguments[i]);
    }
  }
  return update_mask;
}

uint32_t parse_settings(struct settings* settings, int count, char** arguments) {
  return parse_settings_internal(settings, count, arguments, true);
}

uint32_t parse_settings_override(struct settings* settings,
                                 int count,
                                 char** arguments) {
  return parse_settings_internal(settings, count, arguments, false);
}

bool parse_settings_contains_global_filter(int count, char** arguments) {
  static char blacklist[] = "blacklist=";
  static char whitelist[] = "whitelist=";
  if (count <= 0 || !arguments) return false;
  for (int i = 0; i < count; ++i) {
    if (str_starts_with(arguments[i], blacklist)
        || str_starts_with(arguments[i], whitelist)) {
      return true;
    }
  }
  return false;
}

uint32_t parse_settings_apply_target(int count, char** arguments) {
  uint32_t apply_to = 0;
  if (count <= 0 || !arguments) return apply_to;
  for (int i = 0; i < count; ++i) {
    if (!arguments[i]) continue;
    uint32_t candidate = 0;
    if (sscanf(arguments[i], "apply-to=%u", &candidate) == 1) {
      apply_to = candidate;
    }
  }
  return apply_to;
}

bool parse_settings_scope_is_valid(int count, char** arguments) {
  return parse_settings_apply_target(count, arguments) == 0
         || !parse_settings_contains_global_filter(count, arguments);
}
