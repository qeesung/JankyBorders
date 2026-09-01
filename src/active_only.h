#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static inline bool active_only_parse_argument(const char* argument,
                                              bool* enabled) {
  if (!argument || !enabled) return false;

  if (strcmp(argument, "active_only=on") == 0) {
    *enabled = true;
    return true;
  }

  if (strcmp(argument, "active_only=off") == 0) {
    *enabled = false;
    return true;
  }

  return false;
}

static inline bool active_only_should_track_window(bool enabled,
                                                   uint32_t window_id,
                                                   uint32_t active_window_id) {
  return !enabled || window_id == active_window_id;
}

static inline bool active_only_should_remove_window(bool enabled,
                                                    uint32_t window_id,
                                                    uint32_t active_window_id) {
  return enabled && window_id != active_window_id;
}
