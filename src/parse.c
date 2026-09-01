#include "parse.h"
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

static bool parse_color(struct color_style* style, char* token) {
  struct color_style parsed = *style;
  if (sscanf(token, "=0x%x", &parsed.color) == 1) {
    parsed.stype = COLOR_STYLE_SOLID;
    *style = parsed;
    return true;
  }
  else if (sscanf(token, "=glow(0x%x)", &parsed.color) == 1) {
    parsed.stype = COLOR_STYLE_GLOW;
    *style = parsed;
    return true;
  }
  else if (sscanf(token,
             "=gradient(top_left=0x%x,bottom_right=0x%x)",
             &parsed.gradient.color1,
             &parsed.gradient.color2) == 2) {
    parsed.stype = COLOR_STYLE_GRADIENT;
    parsed.gradient.direction = TL_TO_BR;
    *style = parsed;
    return true;
  }
  else if (sscanf(token,
             "=gradient(top_right=0x%x,bottom_left=0x%x)",
             &parsed.gradient.color1,
             &parsed.gradient.color2) == 2) {
    parsed.stype = COLOR_STYLE_GRADIENT;
    parsed.gradient.direction = TR_TO_BL;
    *style = parsed;
    return true;
  }
  else printf("[?] Borders: Invalid color argument color%s\n", token);

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
                                 arguments[i] + strlen(active_color))) {
        update_mask |= BORDER_UPDATE_MASK_ACTIVE;
      }
    }
    else  if (str_starts_with(arguments[i], inactive_color)) {
      if (parse_color(&settings->inactive_window,
                                 arguments[i] + strlen(inactive_color))) {
        update_mask |= BORDER_UPDATE_MASK_INACTIVE;
      }
    }
    else if (str_starts_with(arguments[i], background_color)) {
      if (parse_color(&settings->background,
                                 arguments[i] + strlen(background_color))) {
        update_mask |= BORDER_UPDATE_MASK_ALL;
        settings->show_background = settings->background.color & 0xff000000;
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
    else if (sscanf(arguments[i], "style=%c", &settings->border_style) == 1) {
      update_mask |= BORDER_UPDATE_MASK_ALL;
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
