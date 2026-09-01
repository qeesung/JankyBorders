#include "windows.h"
#include "active_only.h"
#include "adaptive_lifecycle.h"
#include "edge_sampler.h"
#include "hashtable.h"
#include "border.h"
#include "misc/ax.h"
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <libproc.h>

extern pid_t g_pid;
extern struct settings g_settings;
extern struct table g_windows;

#define ADAPTIVE_FOCUS_DEBOUNCE_MS UINT64_C(80)
#define ADAPTIVE_RESIZE_DEBOUNCE_MS UINT64_C(150)
#define ADAPTIVE_RETRY_DELAY_MS UINT64_C(250)
#define ADAPTIVE_MIN_CAPTURE_INTERVAL_MS UINT64_C(250)
#define ADAPTIVE_NS_PER_MS UINT64_C(1000000)

static struct adaptive_pending_capture adaptive_pending;
static struct adaptive_pending_capture adaptive_in_flight_request;
static uint64_t adaptive_generation;
static uint64_t adaptive_serial;
static uint64_t adaptive_last_capture_start_ns;
static bool adaptive_capture_in_flight;
static bool adaptive_attempts_disabled;
static bool adaptive_failure_logged;
static bool adaptive_space_consistency_pass;

static uint64_t windows_adaptive_now_ns(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
  return (uint64_t)now.tv_sec * UINT64_C(1000000000)
         + (uint64_t)now.tv_nsec;
}

static uint64_t windows_adaptive_add_ms(uint64_t now, uint64_t delay_ms) {
  uint64_t delay = delay_ms * ADAPTIVE_NS_PER_MS;
  return now > UINT64_MAX - delay ? UINT64_MAX : now + delay;
}

static uint64_t windows_adaptive_next_generation(void) {
  adaptive_generation = adaptive_lifecycle_next_nonzero(adaptive_generation);
  return adaptive_generation;
}

static uint64_t windows_adaptive_next_serial(void) {
  adaptive_serial = adaptive_lifecycle_next_nonzero(adaptive_serial);
  return adaptive_serial;
}

static bool windows_adaptive_token_is_current(
    struct adaptive_capture_token token,
    struct border** current_border) {
  struct border* border = table_find(&g_windows, &token.wid);
  bool matches = border
                 && adaptive_capture_token_matches(
                     token,
                     border->target_wid,
                     border->adaptive_generation,
                     g_settings.adaptive_color == ADAPTIVE_COLOR_MODE_ACTIVE,
                     border->focused,
                     border->destroying);
  if (current_border) *current_border = matches ? border : NULL;
  return matches;
}

static void windows_adaptive_schedule_pump(uint64_t serial,
                                           uint64_t delay_ns);

static void windows_adaptive_invalidate_border(struct border* border,
                                                bool clear_cache) {
  if (!border) return;
  border->adaptive_generation = windows_adaptive_next_generation();
  adaptive_pending_cancel_window(&adaptive_pending, border->target_wid);
  if (clear_cache && border->adaptive_color_cache.valid_mask) {
    border->adaptive_color_cache.valid_mask = 0;
    border->needs_redraw = true;
  }
}

static void windows_adaptive_clear_all(struct table* windows) {
  adaptive_pending.valid = false;
  (void)windows_adaptive_next_serial();
  if (!windows || !windows->buckets) return;
  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket* bucket = windows->buckets[i];
    while (bucket) {
      struct border* border = bucket->value;
      if (border) {
        windows_adaptive_invalidate_border(border, true);
        if (border->focused) border_update(border, true);
      }
      bucket = bucket->next;
    }
  }
}

static void windows_adaptive_disable_attempts(const char* reason) {
  adaptive_attempts_disabled = true;
  if (!adaptive_failure_logged) {
    fprintf(stderr,
            "[?] Borders: Adaptive color disabled until restart: %s. "
            "Run 'make request-screen-capture', grant access, then "
            "'make service-restart'.\n",
            reason);
    adaptive_failure_logged = true;
  }
  windows_adaptive_clear_all(&g_windows);
}

static bool windows_adaptive_is_transient(enum edge_sampler_status status) {
  return status == EDGE_SAMPLER_WINDOW_NOT_FOUND
         || status == EDGE_SAMPLER_CONTENT_UNAVAILABLE
         || status == EDGE_SAMPLER_CAPTURE_FAILED;
}

static void windows_adaptive_capture_complete(
    const struct edge_sampler_result* result,
    void* context) {
  (void)context;
  struct adaptive_pending_capture finished = adaptive_in_flight_request;
  adaptive_capture_in_flight = false;
  memset(&adaptive_in_flight_request, 0, sizeof(adaptive_in_flight_request));

  if (!result
      || result->wid != finished.token.wid
      || result->generation != finished.token.generation) {
    if (adaptive_pending.valid) {
      windows_adaptive_schedule_pump(adaptive_pending.serial, 0);
    }
    return;
  }

  struct border* border = NULL;
  bool current = windows_adaptive_token_is_current(finished.token, &border);
  if (current && result->status == EDGE_SAMPLER_CAPTURE_PERMISSION_DENIED) {
    windows_adaptive_disable_attempts("screen capture permission is not granted");
  } else if (current && result->status == EDGE_SAMPLER_UNSUPPORTED) {
    windows_adaptive_disable_attempts("ScreenCaptureKit is unavailable");
  } else if (current
             && result->status == EDGE_SAMPLER_OK
             && result->has_analysis
             && result->analysis_status == ADAPTIVE_COLOR_OK) {
    uint8_t next_mask = result->analysis.cache_mask
                        & (uint8_t)((1u << ADAPTIVE_COLOR_SIDE_COUNT) - 1u);
    bool changed = next_mask != border->adaptive_color_cache.valid_mask;
    for (size_t side = 0; side < ADAPTIVE_COLOR_SIDE_COUNT; ++side) {
      uint8_t side_mask = ADAPTIVE_COLOR_SIDE_MASK(side);
      if ((next_mask & side_mask)
          && (!(border->adaptive_color_cache.valid_mask & side_mask)
              || border->adaptive_color_cache.colors[side]
                 != result->analysis.colors[side])) {
        changed = true;
      }
      if (next_mask & side_mask) {
        border->adaptive_color_cache.colors[side]
            = result->analysis.colors[side];
      }
    }
    border->adaptive_color_cache.valid_mask = next_mask;
    if (changed) {
      border->needs_redraw = true;
      border_update(border, true);
    }
  } else if (current
             && windows_adaptive_is_transient(result->status)
             && finished.retry_count == 0
             && !adaptive_pending.valid) {
    uint64_t serial = windows_adaptive_next_serial();
    adaptive_pending_replace(
        &adaptive_pending,
        finished.token,
        serial,
        windows_adaptive_add_ms(windows_adaptive_now_ns(),
                                ADAPTIVE_RETRY_DELAY_MS),
        1);
    windows_adaptive_schedule_pump(serial,
                                   ADAPTIVE_RETRY_DELAY_MS
                                   * ADAPTIVE_NS_PER_MS);
  } else if (current
             && (windows_adaptive_is_transient(result->status)
                 || result->status == EDGE_SAMPLER_INVALID_IMAGE
                 || result->status == EDGE_SAMPLER_ANALYSIS_FAILED)) {
    if (border->adaptive_color_cache.valid_mask) {
      border->adaptive_color_cache.valid_mask = 0;
      border->needs_redraw = true;
      border_update(border, true);
    }
  }

  if (adaptive_pending.valid) {
    windows_adaptive_schedule_pump(adaptive_pending.serial, 0);
  }
}

static void windows_adaptive_capture_pump(uint64_t serial) {
  if (!adaptive_pending.valid || adaptive_pending.serial != serial) return;
  if (g_settings.adaptive_color != ADAPTIVE_COLOR_MODE_ACTIVE
      || adaptive_attempts_disabled) {
    adaptive_pending.valid = false;
    return;
  }
  if (adaptive_capture_in_flight) return;

  uint64_t now = windows_adaptive_now_ns();
  uint64_t earliest = adaptive_pending.ready_at_ns;
  if (adaptive_last_capture_start_ns) {
    uint64_t interval_end = windows_adaptive_add_ms(
        adaptive_last_capture_start_ns,
        ADAPTIVE_MIN_CAPTURE_INTERVAL_MS);
    if (interval_end > earliest) earliest = interval_end;
  }
  if (now < earliest) {
    windows_adaptive_schedule_pump(serial, earliest - now);
    return;
  }

  struct adaptive_pending_capture request = adaptive_pending;
  adaptive_pending.valid = false;
  struct border* border = NULL;
  if (!windows_adaptive_token_is_current(request.token, &border)) return;

  uint32_t fallback[ADAPTIVE_COLOR_SIDE_COUNT];
  border_adaptive_fallback_colors(border, fallback);
  adaptive_capture_in_flight = true;
  adaptive_in_flight_request = request;
  adaptive_last_capture_start_ns = now;
  edge_sampler_capture(request.token.wid,
                       request.token.generation,
                       &border->adaptive_color_cache,
                       fallback,
                       windows_adaptive_capture_complete,
                       NULL);
}

static void windows_adaptive_schedule_pump(uint64_t serial,
                                           uint64_t delay_ns) {
  int64_t dispatch_delay = delay_ns > (uint64_t)INT64_MAX
                           ? INT64_MAX
                           : (int64_t)delay_ns;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, dispatch_delay),
                 dispatch_get_main_queue(), ^{
    windows_adaptive_capture_pump(serial);
  });
}

static void windows_adaptive_schedule_border(struct border* border,
                                             uint64_t debounce_ms) {
  if (!border
      || g_settings.adaptive_color != ADAPTIVE_COLOR_MODE_ACTIVE
      || adaptive_attempts_disabled
      || adaptive_space_consistency_pass
      || !border->focused
      || border->destroying) return;

  struct adaptive_capture_token token = {
    .wid = border->target_wid,
    .generation = windows_adaptive_next_generation(),
  };
  border->adaptive_generation = token.generation;
  uint64_t serial = windows_adaptive_next_serial();
  adaptive_pending_replace(
      &adaptive_pending,
      token,
      serial,
      windows_adaptive_add_ms(windows_adaptive_now_ns(), debounce_ms),
      0);
  windows_adaptive_schedule_pump(serial, debounce_ms * ADAPTIVE_NS_PER_MS);
}

// Loaded via dlsym in main.c
extern CFArrayRef (*JBSLSWindowIteratorGetCornerRadii)(CFTypeRef);

static bool window_in_list(struct table* list, char* app_name) {
  if (table_find(list, app_name)) return true;
  return false;
}

static bool app_allowed(char* app_name) {
  if (g_whitelist_enabled && !window_in_list(&g_whitelist, app_name)) {
    return false;
  }
  if (g_blacklist_enabled && window_in_list(&g_blacklist, app_name)) {
    return false;
  }
  return true;
}

static uint32_t windows_active_window_id(int cid) {
  return g_settings.ax_focus ? ax_get_front_window(cid) : get_front_window(cid);
}

static void windows_remove_all_except(struct table* windows, uint32_t wid) {
  if (!g_settings.active_only) return;

  bool removed_window = false;

  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket** bucket = &windows->buckets[i];
    while (*bucket) {
      struct bucket* current = *bucket;
      struct border* border = current->value;

      if (border
          && active_only_should_remove_window(g_settings.active_only,
                                              border->target_wid,
                                              wid)) {
        *bucket = current->next;
        free(current->key);
        free(current);
        --windows->count;
        windows_adaptive_invalidate_border(border, true);
        border_destroy(border);
        removed_window = true;
      } else {
        bucket = &current->next;
      }
    }
  }

  if (removed_window) windows_update_notifications(windows);
}

bool windows_window_create(struct table* windows, uint32_t wid, uint64_t sid) {
  bool window_created = false;
  int cid = SLSMainConnectionID();
  int wid_cid = 0;
  SLSGetWindowOwner(cid, wid, &wid_cid);

  pid_t pid = 0;
  SLSConnectionGetPID(wid_cid, &pid);
  static char pid_name_buffer[PROC_PIDPATHINFO_MAXSIZE];
  pid_name_buffer[0] = '\0';
  if (proc_name(pid, pid_name_buffer, sizeof(pid_name_buffer)) <= 0) {
    return false;
  }

  if (pid == g_pid
      || g_settings.border_style == BORDER_STYLE_NONE
      || !app_allowed(pid_name_buffer)) {
    return false;
  }
  if (g_settings.active_only
      && !active_only_should_track_window(true,
                                          wid,
                                          windows_active_window_id(cid))) {
    return false;
  }

  CFArrayRef target_ref = cfarray_of_cfnumbers(&wid,
                                               sizeof(uint32_t),
                                               1,
                                               kCFNumberSInt32Type);

  if (!target_ref) return false;

  CFTypeRef query = SLSWindowQueryWindows(cid, target_ref, 0x0);
  if (query) {
    CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
    if (iterator && SLSWindowIteratorGetCount(iterator) > 0) {
      if (SLSWindowIteratorAdvance(iterator)) {
        if (window_suitable(iterator)) {
          struct border* border = table_find(windows, &wid);
          if (!border) {
            border = border_create();
            if (!border || !table_add(windows, &wid, border)) {
              if (border) border_destroy(border);
              if (iterator) CFRelease(iterator);
              CFRelease(query);
              CFRelease(target_ref);
              return false;
            }
            window_created = true;
          }

          int32_t radius = 0;

          // Determine window corner radius
          if (JBSLSWindowIteratorGetCornerRadii) {
            CFArrayRef radii_ref = JBSLSWindowIteratorGetCornerRadii(iterator);
            if (radii_ref && CFArrayGetCount(radii_ref) > 0) {
              CFNumberRef value = CFArrayGetValueAtIndex(radii_ref, 0);
              if (value && CFGetTypeID(value) == CFNumberGetTypeID()) {
                CFNumberGetValue(value, kCFNumberSInt32Type, &radius);
              }
            }
            if (radii_ref) CFRelease(radii_ref);
          }
          radius = radius > 0 ? radius : 9;

          border->radius = radius;
          border->inner_radius = radius + 1;
          border->target_wid = wid;
          border->sid = sid;
          if (g_settings.active_only) border->focused = true;
          border_update(border, false);
          if (border->focused) {
            windows_adaptive_schedule_border(border,
                                             ADAPTIVE_FOCUS_DEBOUNCE_MS);
          }
          windows_update_notifications(windows);
        }
      }
    }
    if (iterator) CFRelease(iterator);
    CFRelease(query);
  }
  CFRelease(target_ref);

  return window_created;
}

static bool windows_remove_all(struct table* windows) {
  if (!windows || !windows->buckets || windows->capacity <= 0) return false;
  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket* bucket = windows->buckets[i];
    while (bucket) {
      if (bucket->value) {
        struct border* border = bucket->value;
        windows_adaptive_invalidate_border(border, true);
        border_destroy(border);
      }
      bucket = bucket->next;
    }
  }
  if (!table_clear(windows)) return false;
  windows_update_notifications(windows);
  return true;
}

void windows_recreate_all_borders(struct table* windows) {
  if (!windows_remove_all(windows)) return;
  windows_add_existing_windows(windows);
  windows_determine_and_focus_active_window(windows);
}

void windows_update_all(struct table* windows) {
  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket* bucket = windows->buckets[i];
    while (bucket) {
      if (bucket->value) {
        struct border* border = bucket->value;
        if (border) {
          border->needs_redraw = true;
          border_update(border, true);
        }
      }
      bucket = bucket->next;
    }
  }
}

void windows_update_active(struct table* windows) {
  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket* bucket = windows->buckets[i];
    while (bucket) {
      if (bucket->value) {
        struct border* border = bucket->value;
        if (border && border->focused) {
          border->needs_redraw = true;
          border_update(border, true);
        }
      }
      bucket = bucket->next;
    }
  }
}

void windows_update_inactive(struct table* windows) {
  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket* bucket = windows->buckets[i];
    while (bucket) {
      if (bucket->value) {
        struct border* border = bucket->value;
        if (border && !border->focused) {
          border->needs_redraw = true;
          border_update(border, true);
        }
      }
      bucket = bucket->next;
    }
  }
}

void windows_window_update(struct table* windows, uint32_t wid) {
  struct border* border = table_find(windows, &wid);
  if (border) border_update(border, true);
}

void windows_window_resize(struct table* windows, uint32_t wid) {
  struct border* border = table_find(windows, &wid);
  if (!border) return;
  border_update(border, true);
  if (border->focused) {
    windows_adaptive_schedule_border(border, ADAPTIVE_RESIZE_DEBOUNCE_MS);
  }
}

static bool windows_window_focus(struct table* windows, uint32_t wid) {
  bool found_window = false;
  struct border* newly_focused = NULL;
  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket* bucket = windows->buckets[i];
    while (bucket) {
      if (bucket->value) {
        struct border* border = bucket->value;
        if (border->focused && border->target_wid != wid) {
          border->focused = false;
          windows_adaptive_invalidate_border(border, false);
          if (!g_settings.active_only) {
            border->needs_redraw = true;
            border_update(border, true);
          }
        }

        if (!border->focused && border->target_wid == wid) {
          border->focused = true;
          newly_focused = border;
          border->needs_redraw = true;
          border_update(border, true);
        }

        if (border->target_wid == wid) found_window = true;
      }
      bucket = bucket->next;
    }
  }

  if (newly_focused) {
    windows_adaptive_schedule_border(newly_focused,
                                     ADAPTIVE_FOCUS_DEBOUNCE_MS);
  }

  return found_window;
}

void windows_window_move(struct table* windows, uint32_t wid) {
  struct border* border = table_find(windows, &wid);
  if (border) border_move(border);
}

void windows_window_hide(struct table* windows, uint32_t wid) {
  struct border* border = table_find(windows, &wid);
  if (border) {
    windows_adaptive_invalidate_border(border, true);
    border_hide(border);
  }
}

void windows_window_unhide(struct table* windows, uint32_t wid) {
  struct border* border = table_find(windows, &wid);
  if (border) {
    if (border->needs_redraw) border_update(border, true);
    border_unhide(border);
    if (border->focused) {
      windows_adaptive_schedule_border(border,
                                       ADAPTIVE_FOCUS_DEBOUNCE_MS);
    }
  }
}

bool windows_window_destroy(struct table* windows, uint32_t wid, uint32_t sid) {
  struct border* border = table_find(windows, &wid);
  if (border && (border->sid == sid || border->sticky || sid == 0)) {
    windows_adaptive_invalidate_border(border, true);
    table_remove(windows, &wid);
    border_destroy(border);
    windows_update_notifications(windows);
    return true;
  }
  return false;
}

void windows_update_notifications(struct table* windows) {
  if (!windows || windows->capacity < 0) return;

  size_t window_count = 0;
  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket* bucket = windows->buckets[i];
    while (bucket) {
      if (bucket->value) {
        if (window_count == INT_MAX) return;
        ++window_count;
      }
      bucket = bucket->next;
    }
  }

  if (window_count > SIZE_MAX / sizeof(uint32_t)) return;
  uint32_t* window_list = window_count
                          ? malloc(window_count * sizeof(uint32_t))
                          : NULL;
  if (window_count && !window_list) return;

  size_t index = 0;
  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket *bucket = windows->buckets[i];
    while (bucket) {
      if (bucket->value && index < window_count) {
        uint32_t wid = *(uint32_t *) bucket->key;
        window_list[index++] = wid;
      }
      bucket = bucket->next;
    }
  }

  int cid = SLSMainConnectionID();
  uint32_t empty_window = 0;
  SLSRequestNotificationsForWindows(cid,
                                    window_list ? window_list : &empty_window,
                                    (int)index);
  free(window_list);
}

void windows_determine_and_focus_active_window(struct table* windows) {
  int cid = SLSMainConnectionID();
  uint32_t front_wid = windows_active_window_id(cid);

  debug("Front window: %d\n", front_wid);
  if (!windows_window_focus(windows, front_wid)) {
    debug("Taking slow window focus path: %d\n", front_wid);
    if (front_wid && windows_window_create(windows,
                                           front_wid,
                                           window_space_id(cid, front_wid))) {
      windows_window_focus(windows, front_wid);
    }
  }
  windows_remove_all_except(windows, front_wid);
}

void windows_adaptive_refresh_active(struct table* windows) {
  if (!windows || !windows->buckets) return;
  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket* bucket = windows->buckets[i];
    while (bucket) {
      struct border* border = bucket->value;
      if (border && border->focused) {
        windows_adaptive_schedule_border(border, 0);
        return;
      }
      bucket = bucket->next;
    }
  }
}

void windows_adaptive_mode_changed(struct table* windows,
                                   enum adaptive_color_mode previous,
                                   enum adaptive_color_mode current) {
  if (previous == current) return;
  if (current == ADAPTIVE_COLOR_MODE_OFF) {
    windows_adaptive_clear_all(windows);
    return;
  }
  windows_adaptive_refresh_active(windows);
}

void windows_adaptive_space_change_started(struct table* windows) {
  adaptive_space_consistency_pass = true;
  adaptive_pending.valid = false;
  (void)windows_adaptive_next_serial();
  if (!windows || !windows->buckets) return;
  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket* bucket = windows->buckets[i];
    while (bucket) {
      struct border* border = bucket->value;
      if (border) windows_adaptive_invalidate_border(border, false);
      bucket = bucket->next;
    }
  }
}

void windows_draw_borders_on_current_spaces(struct table* windows) {
  debug("Space Change: Consistency check\n");

  // Refresh every tracked window first. A native-fullscreen window can be
  // absent from the current-Space query while WindowServer is moving it, but
  // its direct Space lookup may already expose the new SID.
  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket* bucket = windows->buckets[i];
    while (bucket) {
      if (bucket->value) {
        border_retry_space_migration(bucket->value);
        border_update(bucket->value, true);
      }
      bucket = bucket->next;
    }
  }

  int cid = SLSMainConnectionID();

  // Space events are intentionally handled through the caller's delayed
  // consistency pass. Native-fullscreen windows may still have their old SID
  // when the event first arrives, so active-only must not refresh eagerly.
  if (g_settings.active_only) {
    windows_determine_and_focus_active_window(windows);
    return;
  }

  CFArrayRef displays = SLSCopyManagedDisplays(cid);
  if (!displays) return;

  CFIndex space_count_ref = CFArrayGetCount(displays);
  if (space_count_ref <= 0 || space_count_ref > INT_MAX
      || (size_t)space_count_ref > SIZE_MAX / sizeof(uint64_t)) {
    CFRelease(displays);
    return;
  }
  int space_count = (int)space_count_ref;
  uint64_t* space_list = calloc((size_t)space_count, sizeof(uint64_t));
  if (!space_list) {
    CFRelease(displays);
    return;
  }

  for (int i = 0; i < space_count; i++) {
    space_list[i] = SLSManagedDisplayGetCurrentSpace(cid,
                                          CFArrayGetValueAtIndex(displays, i));
  }

  CFRelease(displays);

  CFArrayRef space_list_ref = cfarray_of_cfnumbers(space_list,
                                                   sizeof(uint64_t),
                                                   space_count,
                                                   kCFNumberSInt64Type);
  free(space_list);
  if (!space_list_ref) return;

  uint64_t set_tags = 1;
  uint64_t clear_tags = 0;
  CFArrayRef window_list = SLSCopyWindowsWithOptionsAndTags(cid,
                                                            0,
                                                            space_list_ref,
                                                            0x2,
                                                            &set_tags,
                                                            &clear_tags    );

  if (window_list) {
    CFTypeRef query = SLSWindowQueryWindows(cid, window_list, 0x0);
    if (query) {
      CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
      if (iterator) {
        while(SLSWindowIteratorAdvance(iterator)) {
          if (window_suitable(iterator)) {
            uint32_t wid = SLSWindowIteratorGetWindowID(iterator);
            struct border* border = table_find(windows, &wid);
            if (!border) {
              debug("Creating Missing Window: %d\n", wid);
              windows_window_create(windows, wid, window_space_id(cid, wid));
            }
          }
        }
        CFRelease(iterator);
      }
      CFRelease(query);
    }
    CFRelease(window_list);
  }
  CFRelease(space_list_ref);
}

void windows_refresh_after_space_change(struct table* windows,
                                        bool final_retry) {
  // A real Space event keeps adaptive scheduling suppressed for its whole
  // bounded retry series, including events delivered between consistency
  // passes. Startup and window-create refreshes do not set that state, so
  // their ordinary 80/150 ms triggers remain intact.
  windows_draw_borders_on_current_spaces(windows);
  windows_determine_and_focus_active_window(windows);
  if (final_retry && adaptive_space_consistency_pass) {
    adaptive_space_consistency_pass = false;
    windows_adaptive_refresh_active(windows);
  }
}

static bool append_space_id(uint64_t** space_list,
                            size_t* space_count,
                            size_t* capacity,
                            uint64_t sid) {
  if (!space_list || !space_count || !capacity) return false;
  if (*space_count == *capacity) {
    size_t new_capacity = *capacity ? *capacity * 2 : 16;
    if (new_capacity < *capacity
        || new_capacity > SIZE_MAX / sizeof(uint64_t)) {
      return false;
    }
    uint64_t* resized = realloc(*space_list,
                                new_capacity * sizeof(uint64_t));
    if (!resized) return false;
    *space_list = resized;
    *capacity = new_capacity;
  }
  (*space_list)[(*space_count)++] = sid;
  return true;
}

void windows_add_existing_windows(struct table* windows) {
  int cid = SLSMainConnectionID();

  if (g_settings.active_only) {
    windows_determine_and_focus_active_window(windows);
    return;
  }

  uint64_t* space_list = NULL;
  size_t space_count = 0;
  size_t space_capacity = 0;
  bool allocation_failed = false;

  CFArrayRef display_spaces_ref = SLSCopyManagedDisplaySpaces(cid);
  if (display_spaces_ref) {
    CFIndex display_spaces_count = CFArrayGetCount(display_spaces_ref);
    for (CFIndex i = 0; i < display_spaces_count && !allocation_failed; ++i) {
      CFDictionaryRef display_ref
                               = CFArrayGetValueAtIndex(display_spaces_ref, i);
      if (!display_ref
          || CFGetTypeID(display_ref) != CFDictionaryGetTypeID()) continue;

      CFArrayRef spaces_ref = CFDictionaryGetValue(display_ref,
                                                   CFSTR("Spaces"));
      if (!spaces_ref || CFGetTypeID(spaces_ref) != CFArrayGetTypeID()) continue;

      CFIndex spaces_count = CFArrayGetCount(spaces_ref);
      for (CFIndex j = 0; j < spaces_count; ++j) {
        CFDictionaryRef space_ref = CFArrayGetValueAtIndex(spaces_ref, j);
        if (!space_ref
            || CFGetTypeID(space_ref) != CFDictionaryGetTypeID()) continue;

        CFNumberRef sid_ref = CFDictionaryGetValue(space_ref, CFSTR("id64"));
        uint64_t sid = 0;
        if (!sid_ref || CFGetTypeID(sid_ref) != CFNumberGetTypeID()
            || !CFNumberGetValue(sid_ref, kCFNumberSInt64Type, &sid)) {
          continue;
        }
        if (!append_space_id(&space_list,
                             &space_count,
                             &space_capacity,
                             sid)) {
          allocation_failed = true;
          break;
        }
      }
    }
    CFRelease(display_spaces_ref);
  }

  if (allocation_failed || space_count > INT_MAX) {
    free(space_list);
    return;
  }

  uint64_t set_tags = 1;
  uint64_t clear_tags = 0;

  CFArrayRef space_list_ref = cfarray_of_cfnumbers(space_list,
                                                   sizeof(uint64_t),
                                                   (int)space_count,
                                                   kCFNumberSInt64Type);
  free(space_list);
  if (!space_list_ref) return;

  CFArrayRef window_list_ref = SLSCopyWindowsWithOptionsAndTags(cid,
                                                                0,
                                                                space_list_ref,
                                                                0x2,
                                                                &set_tags,
                                                                &clear_tags  );
  if (window_list_ref) {
    CFIndex count = CFArrayGetCount(window_list_ref);
    if (count > 0) {
      CFTypeRef query = SLSWindowQueryWindows(cid, window_list_ref, 0x0);
      CFTypeRef iterator = query
                           ? SLSWindowQueryResultCopyWindows(query)
                           : NULL;

      while (iterator && SLSWindowIteratorAdvance(iterator)) {
        if (window_suitable(iterator)) {
          uint32_t wid = SLSWindowIteratorGetWindowID(iterator);
          windows_window_create(windows, wid, window_space_id(cid, wid));
        }
      }

      windows_update_notifications(windows);
      if (query) CFRelease(query);
      if (iterator) CFRelease(iterator);
    }
    CFRelease(window_list_ref);
  }
  CFRelease(space_list_ref);
}
