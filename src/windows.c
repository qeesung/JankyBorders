#include "windows.h"
#include "hashtable.h"
#include "border.h"
#include "misc/ax.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <libproc.h>

extern pid_t g_pid;
extern struct settings g_settings;

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

bool windows_window_create(struct table* windows, uint32_t wid, uint64_t sid) {
  bool window_created = false;
  int cid = SLSMainConnectionID();
  int wid_cid = 0;
  SLSGetWindowOwner(cid, wid, &wid_cid);

  pid_t pid = 0;
  SLSConnectionGetPID(wid_cid, &pid);
  static char pid_name_buffer[PROC_PIDPATHINFO_MAXSIZE];
  proc_name(pid, pid_name_buffer, sizeof(pid_name_buffer));

  if (pid == g_pid
      || g_settings.border_style == BORDER_STYLE_NONE
      || !app_allowed(pid_name_buffer)) {
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
          border_update(border, false);
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

static void windows_remove_all(struct table* windows) {
  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket* bucket = windows->buckets[i];
    while (bucket) {
      if (bucket->value) {
        struct border* border = bucket->value;
        border_destroy(border);
      }
      bucket = bucket->next;
    }
  }
  table_clear(windows);
  windows_update_notifications(windows);
}

void windows_recreate_all_borders(struct table* windows) {
  windows_remove_all(windows);
  windows_add_existing_windows(windows);
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

static bool windows_window_focus(struct table* windows, uint32_t wid) {
  bool found_window = false;
  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket* bucket = windows->buckets[i];
    while (bucket) {
      if (bucket->value) {
        struct border* border = bucket->value;
        if (border->focused && border->target_wid != wid) {
          border->focused = false;
          border->needs_redraw = true;
          border_update(border, true);
        }

        if (!border->focused && border->target_wid == wid) {
          border->focused = true;
          border->needs_redraw = true;
          border_update(border, true);
        }

        if (border->target_wid == wid) found_window = true;
      }
      bucket = bucket->next;
    }
  }

  return found_window;
}

void windows_window_move(struct table* windows, uint32_t wid) {
  struct border* border = table_find(windows, &wid);
  if (border) border_move(border);
}

void windows_window_hide(struct table* windows, uint32_t wid) {
  struct border* border = table_find(windows, &wid);
  if (border) border_hide(border);
}

void windows_window_unhide(struct table* windows, uint32_t wid) {
  struct border* border = table_find(windows, &wid);
  if (border) border_unhide(border);
}

bool windows_window_destroy(struct table* windows, uint32_t wid, uint32_t sid) {
  struct border* border = table_find(windows, &wid);
  if (border && (border->sid == sid || border->sticky || sid == 0)) {
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
  uint32_t front_wid = g_settings.ax_focus
                       ? ax_get_front_window(cid)
                       : get_front_window(cid);

  debug("Front window: %d\n", front_wid);
  if (!windows_window_focus(windows, front_wid)) {
    debug("Taking slow window focus path: %d\n", front_wid);
    if (front_wid && windows_window_create(windows,
                                           front_wid,
                                           window_space_id(cid, front_wid))) {
      windows_window_focus(windows, front_wid);
    }
  }
}

void windows_draw_borders_on_current_spaces(struct table* windows) {
  debug("Space Change: Consistency check\n");
  int cid = SLSMainConnectionID();
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
            if (border) border_update(border, true);
            else {
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
