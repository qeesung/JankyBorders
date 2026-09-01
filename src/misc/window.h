#pragma once
#include "extern.h"
#include "helpers.h"
#include "space.h"
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

static inline uint32_t get_front_window(int cid) {
  uint32_t wid = 0;
  uint64_t active_sid = get_active_space_id(cid);
  debug("Active space id: %d\n", active_sid);

  ProcessSerialNumber psn;
  _SLPSGetFrontProcess(&psn);
  int target_cid;
  SLSGetConnectionIDForPSN(cid, &psn, &target_cid);

  uint64_t set_tags = 1;
  uint64_t clear_tags = 0;
  CFArrayRef space_list_ref = cfarray_of_cfnumbers(&active_sid,
                                                   sizeof(uint64_t),
                                                   1,
                                                   kCFNumberSInt64Type);
  if (!space_list_ref) return 0;

  CFArrayRef window_list = SLSCopyWindowsWithOptionsAndTags(cid,
                                                            target_cid,
                                                            space_list_ref,
                                                            0x2,
                                                            &set_tags,
                                                            &clear_tags    );
  if (window_list) {
    uint32_t window_count = CFArrayGetCount(window_list);
    if (window_count > 0) {
      CFTypeRef query = SLSWindowQueryWindows(cid, window_list, 0x0);
      if (query) {
        CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
        if (iterator && SLSWindowIteratorGetCount(iterator) > 0) {
          while (SLSWindowIteratorAdvance(iterator)) {
            if (window_suitable(iterator)) {
              wid = SLSWindowIteratorGetWindowID(iterator);
              break;
            }
          }
        }
        if (iterator) CFRelease(iterator);
        CFRelease(query);
      }
    } else {
      debug("Empty window list\n");
    }
    CFRelease(window_list);
  }
  CFRelease(space_list_ref);
  return wid;
}

static inline uint64_t window_space_id(int cid, uint32_t wid) {
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
    if (count > 0) {
      CFNumberRef id_ref = (CFNumberRef)CFArrayGetValueAtIndex(space_list_ref,
                                                               0             );
      if (id_ref && CFGetTypeID(id_ref) == CFNumberGetTypeID()) {
        CFNumberGetValue(id_ref, kCFNumberSInt64Type, &sid);
      }
    }
    CFRelease(space_list_ref);
  }
  CFRelease(window_list_ref);

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

static inline void window_send_to_space(int cid, uint32_t wid, uint32_t sid) {
  CFArrayRef window_list = cfarray_of_cfnumbers(&wid,
                                                sizeof(uint32_t),
                                                1,
                                                kCFNumberSInt32Type);
  if (!window_list) return;

  SLSMoveWindowsToManagedSpace(cid, window_list, sid);
  CFRelease(window_list);
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
