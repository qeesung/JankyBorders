#pragma once
#include "extern.h"
#include "helpers.h"
#include "space.h"
#include "../space_bridge.h"
#include "../focus_recovery.h"
#include "../window_policy.h"

static inline bool window_suitable(CFTypeRef iterator) {
  uint64_t tags = SLSWindowIteratorGetTags(iterator);
  uint64_t attributes = SLSWindowIteratorGetAttributes(iterator);
  uint32_t parent_wid = SLSWindowIteratorGetParentID(iterator);
  return window_policy_is_suitable(tags, attributes, parent_wid);
}

static inline uint64_t window_tags(int cid, uint32_t wid) {
  uint64_t tags = 0;
  CFArrayRef window_ref = cfarray_of_cfnumbers(&wid,
                                               sizeof(uint32_t),
                                               1,
                                               kCFNumberSInt32Type);
  if (!window_ref) return 0;
  CFTypeRef query = SLSWindowQueryWindows(cid, window_ref, 0x0);
  if (query) {
    CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
    if (iterator
        && SLSWindowIteratorGetCount(iterator) > 0
        && SLSWindowIteratorAdvance(iterator)     ) {
      tags = SLSWindowIteratorGetTags(iterator);
    }
    if (iterator) CFRelease(iterator);
    CFRelease(query);
  }
  CFRelease(window_ref);
  return tags;
}

enum front_window_resolution_status {
  FRONT_WINDOW_RESOLUTION_RESOLVED,
  FRONT_WINDOW_RESOLUTION_COMPLETE_NONE,
  FRONT_WINDOW_RESOLUTION_TRANSIENT,
  FRONT_WINDOW_RESOLUTION_AMBIGUOUS,
};

struct front_window_resolution {
  enum front_window_resolution_status status;
  uint32_t wid;
  uint32_t active_space_wid;
  int front_cid;
  size_t candidate_count;
};

static inline bool front_window_copy_visible_spaces(int cid,
                                                    CFArrayRef* spaces_out) {
  if (!spaces_out) return false;
  *spaces_out = NULL;

  CFArrayRef displays = SLSCopyManagedDisplays(cid);
  if (!displays) return false;

  CFIndex display_count = CFArrayGetCount(displays);
  if (display_count <= 0
      || display_count > INT_MAX
      || (size_t)display_count > SIZE_MAX / sizeof(uint64_t)) {
    CFRelease(displays);
    return false;
  }

  uint64_t* space_ids = calloc((size_t)display_count, sizeof(uint64_t));
  if (!space_ids) {
    CFRelease(displays);
    return false;
  }

  int space_count = 0;
  bool complete = true;
  for (CFIndex i = 0; i < display_count; ++i) {
    CFTypeRef display = CFArrayGetValueAtIndex(displays, i);
    if (!display || CFGetTypeID(display) != CFStringGetTypeID()) {
      complete = false;
      break;
    }

    uint64_t sid = SLSManagedDisplayGetCurrentSpace(cid,
                                                     (CFStringRef)display);
    if (!sid) {
      complete = false;
      break;
    }

    bool duplicate = false;
    for (int j = 0; j < space_count; ++j) {
      if (space_ids[j] == sid) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) space_ids[space_count++] = sid;
  }
  CFRelease(displays);

  if (complete && space_count > 0) {
    *spaces_out = cfarray_of_cfnumbers(space_ids,
                                       sizeof(uint64_t),
                                       space_count,
                                       kCFNumberSInt64Type);
  }
  free(space_ids);
  return *spaces_out != NULL;
}

static inline bool front_window_copy_suitable_candidates(
    int cid,
    int front_cid,
    CFArrayRef spaces,
    uint32_t** candidates_out,
    size_t* candidate_count_out) {
  if (!spaces || !candidates_out || !candidate_count_out) return false;
  *candidates_out = NULL;
  *candidate_count_out = 0;

  uint64_t set_tags = 1;
  uint64_t clear_tags = 0;
  CFArrayRef window_list = SLSCopyWindowsWithOptionsAndTags(cid,
                                                            front_cid,
                                                            spaces,
                                                            0x2,
                                                            &set_tags,
                                                            &clear_tags    );
  if (!window_list) return false;

  CFIndex window_count = CFArrayGetCount(window_list);
  if (window_count == 0) {
    CFRelease(window_list);
    return true;
  }
  if (window_count < 0
      || (size_t)window_count > SIZE_MAX / sizeof(uint32_t)) {
    CFRelease(window_list);
    return false;
  }

  CFTypeRef query = SLSWindowQueryWindows(cid, window_list, 0x0);
  if (!query) {
    CFRelease(window_list);
    return false;
  }
  CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
  if (!iterator) {
    CFRelease(query);
    CFRelease(window_list);
    return false;
  }

  uint32_t* candidates = calloc((size_t)window_count, sizeof(uint32_t));
  if (!candidates) {
    CFRelease(iterator);
    CFRelease(query);
    CFRelease(window_list);
    return false;
  }

  size_t candidate_count = 0;
  while (SLSWindowIteratorAdvance(iterator)) {
    if (!window_suitable(iterator)) continue;

    uint32_t wid = SLSWindowIteratorGetWindowID(iterator);
    if (!wid) continue;
    bool ordered = false;
    if (SLSWindowIsOrderedIn(cid, wid, &ordered) != kCGErrorSuccess) {
      free(candidates);
      CFRelease(iterator);
      CFRelease(query);
      CFRelease(window_list);
      return false;
    }
    if (!ordered) continue;

    bool duplicate = false;
    for (size_t i = 0; i < candidate_count; ++i) {
      if (candidates[i] == wid) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate && candidate_count < (size_t)window_count) {
      candidates[candidate_count++] = wid;
    }
  }

  CFRelease(iterator);
  CFRelease(query);
  CFRelease(window_list);

  if (!candidate_count) {
    free(candidates);
    candidates = NULL;
  }
  *candidates_out = candidates;
  *candidate_count_out = candidate_count;
  return true;
}

static inline uint32_t front_window_unique_active_space_candidate(
    int cid,
    int front_cid) {
  uint64_t active_sid = get_active_space_id(cid);
  debug("Active space id: %llu\n", (unsigned long long)active_sid);
  if (!active_sid) return 0;

  CFArrayRef active_space = cfarray_of_cfnumbers(&active_sid,
                                                 sizeof(uint64_t),
                                                 1,
                                                 kCFNumberSInt64Type);
  if (!active_space) return 0;

  uint32_t* candidates = NULL;
  size_t candidate_count = 0;
  bool complete = front_window_copy_suitable_candidates(cid,
                                                         front_cid,
                                                         active_space,
                                                         &candidates,
                                                         &candidate_count);
  CFRelease(active_space);

  uint32_t wid = complete && candidate_count == 1 ? candidates[0] : 0;
  free(candidates);
  return wid;
}

static inline struct front_window_resolution resolve_front_window(int cid) {
  struct front_window_resolution result = {
    .status = FRONT_WINDOW_RESOLUTION_TRANSIENT,
    .wid = 0,
    .active_space_wid = 0,
    .front_cid = 0,
    .candidate_count = 0,
  };

  ProcessSerialNumber psn;
  if (_SLPSGetFrontProcess(&psn) != noErr) return result;
  if (SLSGetConnectionIDForPSN(cid, &psn, &result.front_cid)
      != kCGErrorSuccess
      || result.front_cid <= 0) {
    return result;
  }

  CFArrayRef visible_spaces = NULL;
  if (!front_window_copy_visible_spaces(cid, &visible_spaces)) return result;

  uint32_t* candidates = NULL;
  if (!front_window_copy_suitable_candidates(cid,
                                              result.front_cid,
                                              visible_spaces,
                                              &candidates,
                                              &result.candidate_count)) {
    CFRelease(visible_spaces);
    return result;
  }
  CFRelease(visible_spaces);

  if (!result.candidate_count) {
    result.status = FRONT_WINDOW_RESOLUTION_COMPLETE_NONE;
    return result;
  }

  CFArrayRef z_order = CGWindowListCreate(
      kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
      kCGNullWindowID);
  bool z_order_usable = false;
  if (z_order) {
    CFIndex z_count = CFArrayGetCount(z_order);
    uint32_t* ordered_windows = NULL;
    size_t ordered_count = 0;
    if (z_count > 0 && (size_t)z_count <= SIZE_MAX / sizeof(uint32_t)) {
      ordered_windows = calloc((size_t)z_count, sizeof(uint32_t));
    }
    if (z_count == 0 || ordered_windows) {
      z_order_usable = true;
      for (CFIndex i = 0; i < z_count; ++i) {
        // CGWindowListCreate stores CGWindowID values as pointer-sized array
        // entries, not retained CFNumber objects. Calling CFGetTypeID on these
        // small integer pointers crashes.
        uintptr_t raw_wid = (uintptr_t)CFArrayGetValueAtIndex(z_order, i);
        if (raw_wid > 0 && raw_wid <= UINT32_MAX) {
          ordered_windows[ordered_count++] = (uint32_t)raw_wid;
        }
      }
      result.wid = focus_recovery_select_frontmost(
          ordered_windows,
          ordered_count,
          candidates,
          result.candidate_count);
      result.status = result.wid
                      ? FRONT_WINDOW_RESOLUTION_RESOLVED
                      : FRONT_WINDOW_RESOLUTION_TRANSIENT;
    }
    free(ordered_windows);
    CFRelease(z_order);
    if (result.status == FRONT_WINDOW_RESOLUTION_RESOLVED) {
      free(candidates);
      return result;
    }
    // A present CG list that we could not parse (for example, allocation
    // failure) is a local transient failure, not permission to downgrade to
    // the less reliable active-menu-display fallback.
    if (!z_order_usable) {
      free(candidates);
      return result;
    }
  }

  // The public on-screen list can temporarily omit a single window while
  // Spaces or native fullscreen are settling. Multiple candidates with a
  // successfully read z-order but no intersection are inconsistent: do not
  // revive the old active-menu-display heuristic in that case. Only use its
  // stable two-snapshot fallback when the public z-order was unavailable.
  if (result.candidate_count == 1) {
    result.status = FRONT_WINDOW_RESOLUTION_RESOLVED;
    result.wid = candidates[0];
  } else if (z_order_usable) {
    // The process is known and has multiple suitable windows, but none can be
    // selected safely. Keep the active-Space hint empty so callers can retire
    // an old app at the final retry without guessing a display.
    result.status = FRONT_WINDOW_RESOLUTION_AMBIGUOUS;
  } else {
    result.status = FRONT_WINDOW_RESOLUTION_AMBIGUOUS;
    result.active_space_wid = front_window_unique_active_space_candidate(
        cid,
        result.front_cid);
  }
  free(candidates);
  return result;
}

static inline uint32_t get_front_window(int cid) {
  struct front_window_resolution result = resolve_front_window(cid);
  if (result.status == FRONT_WINDOW_RESOLUTION_RESOLVED) {
    return result.wid;
  }
  return 0;
}

static inline uint64_t window_direct_space_id(int cid, uint32_t wid) {
  uint64_t sid = 0;

  CFArrayRef window_list_ref = cfarray_of_cfnumbers(&wid,
                                                    sizeof(uint32_t),
                                                    1,
                                                    kCFNumberSInt32Type);
  if (!window_list_ref) return 0;

  CFArrayRef space_list_ref = SLSCopySpacesForWindows(cid,
                                                      0x7,
                                                      window_list_ref);


  if (space_list_ref) {
    int count = CFArrayGetCount(space_list_ref);
    for (int i = 0; i < count; ++i) {
      CFNumberRef id_ref = (CFNumberRef)CFArrayGetValueAtIndex(space_list_ref,
                                                               i             );
      if (!id_ref || CFGetTypeID(id_ref) != CFNumberGetTypeID()) continue;
      uint64_t candidate_sid = 0;
      if (!CFNumberGetValue(id_ref,
                            kCFNumberSInt64Type,
                            &candidate_sid)) {
        continue;
      }
      if (!sid) sid = candidate_sid;
      if (is_space_visible(cid, candidate_sid)) {
        sid = candidate_sid;
        break;
      }
    }
    CFRelease(space_list_ref);
  }
  CFRelease(window_list_ref);

  return sid;
}

static inline uint64_t window_space_id(int cid, uint32_t wid) {
  uint64_t sid = window_direct_space_id(cid, wid);

  if (sid) return sid;

  CFStringRef uuid = SLSCopyManagedDisplayForWindow(cid, wid);
  if (uuid) {
    uint64_t sid = SLSManagedDisplayGetCurrentSpace(cid, uuid);
    CFRelease(uuid);
    return sid;
  }

  return 0;
}

extern mach_port_t g_server_port;
static inline int32_t window_sub_level(int cid, uint32_t wid) {
  (void)cid;
  mach_msg_id_t request = 0x73c3;
  if (__builtin_available(macOS 26.0, *)) request = 0x76e3;

  mach_msg_id_t response = 0x7427;
  if (__builtin_available(macOS 26.0, *)) response = 0x7747;

  #pragma pack(push,2)
  struct {
    struct {
      mach_msg_header_t header;
      NDR_record_t NDR_record;
    } info;

    struct {
      int32_t wid;
    } payload;

    struct {
      int32_t sub_level;
      int64_t padding;
    } response;
  } msg = { 0 };
  #pragma pack(pop)

  msg.info.NDR_record = NDR_record;
  msg.info.header.msgh_remote_port = g_server_port;
  msg.info.header.msgh_local_port = mig_get_special_reply_port();
  msg.info.header.msgh_bits = MACH_MSGH_BITS_SET(MACH_MSG_TYPE_COPY_SEND,
                                                 MACH_MSG_TYPE_MAKE_SEND_ONCE,
                                                 0,
                                                 MACH_MSGH_BITS_REMOTE_MASK  );

  msg.info.header.msgh_id = request;
  msg.payload.wid = wid;

  kern_return_t error = mach_msg(&msg.info.header,
                                 MACH_SEND_MSG
                                 | MACH_SEND_SYNC_OVERRIDE
                                 | MACH_SEND_PROPAGATE_QOS
                                 | MACH_RCV_MSG
                                 | MACH_RCV_SYNC_WAIT,
                                 sizeof(msg.info) + sizeof(msg.payload),
                                 sizeof(msg),
                                 msg.info.header.msgh_local_port,
                                 MACH_MSG_TIMEOUT_NONE,
                                 MACH_PORT_NULL                         );
  
  if (error != KERN_SUCCESS) {
    printf("SubLevel: Error receiving message.\n");
    mig_dealloc_special_reply_port(msg.info.header.msgh_local_port);
    return 0;
  }

  if (msg.info.header.msgh_id != response) {
    printf("SubLevel: Invalid message received\n");
    mach_msg_destroy(&msg.info.header);
    return 0;
  }

  return msg.response.sub_level;
}

static inline int window_level(int cid, uint32_t wid) {
  CFArrayRef target_ref = cfarray_of_cfnumbers(&wid,
                                               sizeof(uint32_t),
                                               1,
                                               kCFNumberSInt32Type );
  if (!target_ref) return 0;

  CFTypeRef query = SLSWindowQueryWindows(cid, target_ref, 0x0);
  CFTypeRef iterator = query ? SLSWindowQueryResultCopyWindows(query) : NULL;
  int level = 0;
  if (iterator && SLSWindowIteratorAdvance(iterator)) {
    level = SLSWindowIteratorGetLevel(iterator);
  }
  if (iterator) CFRelease(iterator);

  if (query) CFRelease(query);
  CFRelease(target_ref);

  return level;
}

static inline void window_send_to_space(int cid, uint32_t wid, uint64_t sid) {
  CFArrayRef window_list = cfarray_of_cfnumbers(&wid,
                                                sizeof(uint32_t),
                                                1,
                                                kCFNumberSInt32Type);
  if (!window_list) return;

  SLSMoveWindowsToManagedSpace(cid, window_list, sid);
  CFRelease(window_list);
}

static inline void window_recover_to_space(int cid,
                                           uint32_t wid,
                                           uint64_t sid) {
  if (__builtin_available(macOS 14.5, *)) {
    // The private bridged operation is reserved for a helper whose actual SID
    // was verified to differ from its target. Submission is asynchronous; the
    // bounded refresh loop reads the actual SID again before declaring success.
    if (space_bridge_move_window(wid, sid)) {
      return;
    }
  }
  window_send_to_space(cid, wid, sid);
}

static inline uint32_t window_create(int cid, CGRect frame, bool hidpi, bool unmanaged) {
  uint32_t id = 0;
  CFTypeRef frame_region = NULL;
  uint64_t set_tags = (1ULL << 1) | (1ULL << 9);
  uint64_t clear_tags = 0;

  CGSNewRegionWithRect(&frame, &frame_region);
  if (!frame_region) return 0;

  if (unmanaged) {
    CFTypeRef empty_region = CGRegionCreateEmptyRegion();
    if (!empty_region) {
      CFRelease(frame_region);
      return 0;
    }
    SLSNewWindowWithOpaqueShapeAndContext(cid,
                                          kCGBackingStoreBuffered,
                                          frame_region,
                                          empty_region,
                                          13 | (1 << 18),
                                          &set_tags,
                                          -9999,
                                          -9999,
                                          64,
                                          &id,
                                          NULL                    );
    if (id) SLSSetWindowAlpha(cid, id, 0.f);
    CFRelease(empty_region);
  } else {
    SLSNewWindow(cid,
                 kCGBackingStoreBuffered,
                 -9999,
                 -9999,
                 frame_region,
                 &id                     );
  }
  CFRelease(frame_region);

  uint32_t wid = id;
  if (!wid) return 0;

  SLSSetWindowResolution(cid, wid, hidpi ? 2.0f : 1.0f);
  SLSSetWindowTags(cid, wid, &set_tags, 64);
  SLSClearWindowTags(cid, wid, &clear_tags, 64);
  SLSSetWindowOpacity(cid, wid, 0);

  CFIndex shadow_density = 0;
  CFNumberRef shadow_density_cf = CFNumberCreate(kCFAllocatorDefault,
                                                 kCFNumberCFIndexType,
                                                 &shadow_density      );
  if (shadow_density_cf) {
    const void *keys[1] = { CFSTR("com.apple.WindowShadowDensity") };
    const void *values[1] = { shadow_density_cf };
    CFDictionaryRef shadow_props_cf = CFDictionaryCreate(NULL,
                                               keys,
                                               values,
                                               1,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);

    if (shadow_props_cf) {
      SLSWindowSetShadowProperties(wid, shadow_props_cf);
      CFRelease(shadow_props_cf);
    }
    CFRelease(shadow_density_cf);
  }

  return wid;
}
