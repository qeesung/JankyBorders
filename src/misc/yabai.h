#pragma once
#include <dispatch/dispatch.h>
#define _YABAI_INTEGRATION

#ifdef _YABAI_INTEGRATION
#include "extern.h"
#include "../windows.h"
#include "../mach.h"
#include <CoreVideo/CoreVideo.h>
#include <math.h>
#include <pthread.h>

// Additional border interfaces needed for the yabai integration
bool border_init(struct border* border, int cid);
void border_create_window(struct border* border, CGRect frame, bool unmanaged, bool hidpi);
bool border_update_internal(struct border* border, struct settings* settings);

struct track_transform_payload {
  int cid;
  uint32_t border_wid;
  uint32_t proxy_wid;
  uint32_t target_wid;
  CGAffineTransform initial_transform;
};

static CVReturn track_transform(CVDisplayLinkRef display_link,
                                const CVTimeStamp* now,
                                const CVTimeStamp* output_time,
                                CVOptionFlags flags,
                                CVOptionFlags* flags_out,
                                void* context) {
  (void)display_link;
  (void)now;
  (void)output_time;
  (void)flags;
  (void)flags_out;
  struct animation* animation = context;
  usleep(0.25*animation->frame_time);

  struct track_transform_payload* payload = animation->context;
  CGAffineTransform target_transform, border_transform;
  CGError error = SLSGetWindowTransform(payload->cid,
                                        payload->target_wid,
                                        &target_transform   );

  if (error != kCGErrorSuccess) return kCVReturnSuccess;

  border_transform = CGAffineTransformConcat(target_transform,
                                             payload->initial_transform);

  CFTypeRef transaction = SLSTransactionCreate(payload->cid);
  if (transaction) {
    SLSTransactionSetWindowTransform(transaction, payload->proxy_wid, 0, 0, border_transform);
    SLSTransactionSetWindowTransform(transaction, payload->border_wid, 0, 0, border_transform);
    SLSTransactionCommit(transaction, 0);
    CFRelease(transaction);
  }
  return kCVReturnSuccess;
}

static struct border* yabai_proxy_create_candidate(struct border* border) {
  struct border* proxy = malloc(sizeof(struct border));
  if (!proxy) return NULL;
  if (!border_init(proxy, border->cid)) {
    free(proxy);
    return NULL;
  }

  // Proxy borders share their parent's SkyLight connection. Mark ownership
  // before any fallible helper-window operation so failure cleanup never
  // releases the parent's connection.
  proxy->is_proxy = true;
  proxy->target_wid = border->target_wid;
  proxy->sid = border->sid;
  border_create_window(proxy, CGRectNull, true, false);
  if (!proxy->wid || !proxy->context) {
    border_destroy(proxy);
    return NULL;
  }

  // border_create_window records its creation frame. Restore the parent's
  // geometry afterwards because it seeds the transform-tracking payload.
  proxy->target_bounds = border->target_bounds;
  proxy->frame = border->frame;
  proxy->focused = border->focused;
  proxy->radius = border->radius;
  proxy->inner_radius = border->inner_radius;
  window_send_to_space(proxy->cid, proxy->wid, proxy->sid);
  return proxy;
}

static bool yabai_restore_parent_alpha(struct border* border) {
  CFTypeRef transaction = SLSTransactionCreate(border->cid);
  CGError error = kCGErrorFailure;
  if (transaction) {
    error = SLSTransactionSetWindowAlpha(transaction, border->wid, 1.f);
    if (error == kCGErrorSuccess) {
      error = SLSTransactionCommit(transaction, 0);
    }
    CFRelease(transaction);
  }
  if (error != kCGErrorSuccess) {
    error = SLSSetWindowAlpha(border->cid, border->wid, 1.f);
  }
  return error == kCGErrorSuccess;
}

static void yabai_retry_parent_alpha(struct table* windows,
                                     uint32_t real_wid,
                                     unsigned int attempt) {
  if (attempt >= 3) return;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                               (int64_t)(attempt + 1)
                                 * 100 * NSEC_PER_MSEC),
                 dispatch_get_main_queue(),
                 ^{
    uint32_t target_wid = real_wid;
    struct border* border = table_find(windows, &target_wid);
    if (!border) return;

    pthread_mutex_lock(&border->mutex);
    if (border->proxy || border->external_proxy_wid) {
      pthread_mutex_unlock(&border->mutex);
      return;
    }
    bool restored = yabai_restore_parent_alpha(border);
    if (restored) {
      struct settings settings = *border_get_settings(border);
      border_update_internal(border, &settings);
    }
    pthread_mutex_unlock(&border->mutex);
    if (!restored) {
      yabai_retry_parent_alpha(windows, real_wid, attempt + 1);
    }
  });
}

static bool yabai_proxy_activate(struct border* border,
                                 struct border* proxy,
                                 uint32_t external_proxy_wid,
                                 struct settings* settings) {
  pthread_mutex_lock(&proxy->mutex);

  CGRect proxy_frame = CGRectNull;
  if (SLSGetWindowBounds(proxy->cid,
                         external_proxy_wid,
                         &proxy_frame) != kCGErrorSuccess
      || !isfinite(proxy_frame.size.width)
      || !isfinite(proxy_frame.size.height)
      || proxy_frame.size.width <= 0.0
      || proxy_frame.size.height <= 0.0) {
    pthread_mutex_unlock(&proxy->mutex);
    return false;
  }

  struct track_transform_payload* payload = malloc(sizeof(*payload));
  if (!payload) {
    pthread_mutex_unlock(&proxy->mutex);
    return false;
  }

  payload->proxy_wid = proxy->wid;
  payload->border_wid = border->wid;
  payload->target_wid = external_proxy_wid;
  payload->cid = proxy->cid;
  payload->initial_transform = CGAffineTransformIdentity;
  payload->initial_transform.a = proxy->target_bounds.size.width
                                / proxy_frame.size.width;
  payload->initial_transform.d = proxy->target_bounds.size.height
                                / proxy_frame.size.height;
  payload->initial_transform.tx = 0.5 * (proxy->frame.size.width
                                  - proxy->target_bounds.size.width);
  payload->initial_transform.ty = 0.5 * (proxy->frame.size.height
                                  - proxy->target_bounds.size.height);

  animation_stop(&proxy->animation);
  if (!animation_start(&proxy->animation, track_transform, payload)) {
    pthread_mutex_unlock(&proxy->mutex);
    return false;
  }

  proxy->frame = CGRectNull;
  if (!border_update_internal(proxy, settings)) {
    animation_stop(&proxy->animation);
    pthread_mutex_unlock(&proxy->mutex);
    return false;
  }

  CFTypeRef transaction = SLSTransactionCreate(proxy->cid);
  if (!transaction) {
    animation_stop(&proxy->animation);
    pthread_mutex_unlock(&proxy->mutex);
    return false;
  }
  CGError setup_error = SLSTransactionOrderWindow(transaction,
                                                   proxy->wid,
                                                   proxy->effective_order,
                                                   external_proxy_wid);
  if (setup_error == kCGErrorSuccess) {
    setup_error = SLSTransactionSetWindowAlpha(transaction,
                                               border->wid,
                                               0.f);
  }
  if (setup_error == kCGErrorSuccess) {
    setup_error = SLSTransactionSetWindowAlpha(transaction,
                                               proxy->wid,
                                               1.f);
  }
  CGError error = setup_error == kCGErrorSuccess
                  ? SLSTransactionCommit(transaction, 0)
                  : setup_error;
  CFRelease(transaction);
  if (error != kCGErrorSuccess) {
    animation_stop(&proxy->animation);
    pthread_mutex_unlock(&proxy->mutex);
    return false;
  }

  pthread_mutex_unlock(&proxy->mutex);
  return true;
}

static void yabai_proxy_begin_on_main(struct table* windows,
                                      uint32_t wid,
                                      uint32_t real_wid) {
  struct border* border = table_find(windows, &real_wid);
  if (!border) return;

  pthread_mutex_lock(&border->mutex);
  if (!border->wid || !border->context) {
    pthread_mutex_unlock(&border->mutex);
    return;
  }
  struct settings settings = *border_get_settings(border);
  struct border* previous_proxy = border->proxy;
  struct border* candidate = yabai_proxy_create_candidate(border);
  bool restore_failed = false;
  if (!candidate
      || !yabai_proxy_activate(border, candidate, wid, &settings)) {
    if (!previous_proxy) {
      border->external_proxy_wid = 0;
      restore_failed = !yabai_restore_parent_alpha(border);
      border_update_internal(border, &settings);
    }
    pthread_mutex_unlock(&border->mutex);
    if (candidate) border_destroy(candidate);
    if (restore_failed) yabai_retry_parent_alpha(windows, real_wid, 0);
    return;
  }

  border->proxy = candidate;
  border->external_proxy_wid = wid;
  if (previous_proxy) {
    pthread_mutex_lock(&previous_proxy->mutex);
    animation_stop(&previous_proxy->animation);
    pthread_mutex_unlock(&previous_proxy->mutex);
    border_destroy(previous_proxy);
  }
  pthread_mutex_unlock(&border->mutex);
}

static inline void yabai_proxy_begin(struct table* windows,
                                     uint32_t wid,
                                     uint32_t real_wid) {
  if (!windows || !real_wid || !wid) return;
  dispatch_async(dispatch_get_main_queue(), ^{
    yabai_proxy_begin_on_main(windows, wid, real_wid);
  });
}

static void yabai_proxy_end_on_main(struct table* windows,
                                    uint32_t wid,
                                    uint32_t real_wid,
                                    unsigned int attempt) {
  struct border* border = table_find(windows, &real_wid);
  if (!border) return;

  pthread_mutex_lock(&border->mutex);
  if (!border->proxy || border->external_proxy_wid != wid) {
    pthread_mutex_unlock(&border->mutex);
    return;
  }

  struct border* proxy = border->proxy;
  if (!yabai_restore_parent_alpha(border)) {
    pthread_mutex_unlock(&border->mutex);
    if (attempt < 2) {
      dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                                   (int64_t)(attempt + 1)
                                     * 100 * NSEC_PER_MSEC),
                     dispatch_get_main_queue(),
                     ^{
        yabai_proxy_end_on_main(windows, wid, real_wid, attempt + 1);
      });
    }
    return;
  }
  border->proxy = NULL;
  border->external_proxy_wid = 0;

  pthread_mutex_lock(&proxy->mutex);
  animation_stop(&proxy->animation);
  SLSSetWindowAlpha(border->cid, proxy->wid, 0.f);
  pthread_mutex_unlock(&proxy->mutex);

  struct settings settings = *border_get_settings(border);
  border->event_buffer.disable_coalescing = true;
  border_update_internal(border, &settings);
  border->event_buffer.disable_coalescing = false;
  pthread_mutex_unlock(&border->mutex);
  border_destroy(proxy);
}

static inline void yabai_proxy_end(struct table* windows,
                                   uint32_t wid,
                                   uint32_t real_wid) {
  if (!windows || !real_wid || !wid) return;
  dispatch_async(dispatch_get_main_queue(), ^{
    yabai_proxy_end_on_main(windows, wid, real_wid, 0);
  });
}

static void yabai_message(CFMachPortRef port, void* data, CFIndex size, void* context) {
  (void)port;
  if (size < 0) return;

  void* payload_data = NULL;
  uint32_t payload_size = 0;
  if (!mach_message_get_payload(data,
                                (size_t)size,
                                &payload_data,
                                &payload_size)) {
    mach_destroy_received_message(data, (size_t)size);
    return;
  }

  struct payload {
    uint32_t event;
    uint32_t count;
    uint32_t proxy_wid[512];
    uint32_t real_wid[512];
  };

  if (payload_size == sizeof(struct payload) && payload_data && context) {
    struct payload* payload = payload_data;
    // A count larger than the wire-format arrays means the whole event is
    // malformed. Reject it instead of executing a truncated subset.
    if (payload->count <= 512) {
      if (payload->event == 1325) {
        for (uint32_t i = 0; i < payload->count; i++) {
          yabai_proxy_begin(context,
                            payload->proxy_wid[i],
                            payload->real_wid[i]  );
        }
      } else if (payload->event == 1326) {
        for (uint32_t i = 0; i < payload->count; i++) {
          yabai_proxy_end(context,
                          payload->proxy_wid[i],
                          payload->real_wid[i]  );
        }
      }
    }
  }
  mach_destroy_received_message(data, (size_t)size);
}

static inline void yabai_register_mach_port(struct table* windows) {
  ipc_space_t task = mach_task_self();
  mach_port_t port = MACH_PORT_NULL;
  CFMachPortRef cf_mach_port = NULL;
  CFRunLoopSourceRef source = NULL;
  if (mach_port_allocate(task,
                         MACH_PORT_RIGHT_RECEIVE,
                         &port                   ) != KERN_SUCCESS) {
    return;
  }

  struct mach_port_limits limits = { 1 };
  if (mach_port_set_attributes(task,
                               port,
                               MACH_PORT_LIMITS_INFO,
                               (mach_port_info_t)&limits,
                               MACH_PORT_LIMITS_INFO_COUNT) != KERN_SUCCESS) {
    goto registration_failed;
  }

  if (mach_port_insert_right(task,
                             port,
                             port,
                             MACH_MSG_TYPE_MAKE_SEND) != KERN_SUCCESS) {
    goto registration_failed;
  }

  CFMachPortContext context = {
    .version = 0,
    .info = (void*)windows,
    .retain = NULL,
    .release = NULL,
    .copyDescription = NULL
  };

  cf_mach_port = CFMachPortCreateWithPort(NULL,
                                         port,
                                         yabai_message,
                                         &context,
                                         false         );
  if (!cf_mach_port) goto registration_failed;

  source = CFMachPortCreateRunLoopSource(NULL, cf_mach_port, 0);
  if (!source) goto registration_failed;

  if (!mach_register_port(port, "git.felix.jbevent")) {
    goto registration_failed;
  }

  CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopDefaultMode);
  CFRelease(source);
  CFRelease(cf_mach_port);
  return;

registration_failed:
  if (source) CFRelease(source);
  if (cf_mach_port) CFRelease(cf_mach_port);
  mach_dispose_port(task, &port);
}
#endif
