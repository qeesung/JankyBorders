#include "events.h"
#include "misc/extern.h"
#include "windows.h"
#include "border.h"
#include "focus_recovery.h"
#include "misc/window.h"
#include "space_recovery.h"
#include <string.h>

extern struct table g_windows;
extern pid_t g_pid;

static volatile uint64_t focus_change_generation;
static volatile bool space_transition_active;
// SkyLight notifications are drained by the main CFRunLoop in main.c and all
// delayed probes target the main dispatch queue, so this state is serialized.
static struct focus_recovery_scheduler focus_refresh_scheduler;

static void events_cancel_focus_refresh(void) {
  __sync_add_and_fetch(&focus_change_generation, 1);
  focus_recovery_scheduler_cancel(&focus_refresh_scheduler);
  windows_focus_probe_reset();
}

static void events_schedule_focus_refresh(bool invalidate_fallback) {
  if (!focus_recovery_scheduler_schedule(&focus_refresh_scheduler)) {
    // The in-flight callbacks always read current WindowServer state, so a
    // burst must not postpone the already scheduled probe. Structural/order
    // events can mean a same-process window switch, so they invalidate the
    // fallback identity; noisy title/content updates leave it intact.
    if (invalidate_fallback) windows_focus_probe_reset();
    return;
  }

  uint64_t generation = __sync_add_and_fetch(&focus_change_generation, 1);
  windows_focus_probe_reset();

  for (size_t i = 0; i < FOCUS_RECOVERY_RETRY_COUNT; ++i) {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                                 focus_recovery_retry_delays_us[i]
                                 * NSEC_PER_USEC),
                   dispatch_get_main_queue(), ^{
      if (!focus_recovery_retry_is_current(
              __sync_fetch_and_add(&focus_change_generation, 0),
              generation)) {
        return;
      }

      bool final_retry = i == FOCUS_RECOVERY_RETRY_COUNT - 1;
      // Keep the dirty bit latched for the whole series. If the last attempt
      // still has to HOLD, any coalesced event warrants exactly one fresh
      // bounded series; clearing it on an earlier probe could lose that rearm.
      bool observed_new_event = final_retry
                                && focus_recovery_scheduler_observe_events(
                                    &focus_refresh_scheduler);
      enum windows_focus_refresh_result result = windows_refresh_active_window(
          &g_windows,
          final_retry && !space_transition_active);
      if (final_retry) {
        bool rearm = focus_recovery_scheduler_finish(
            &focus_refresh_scheduler,
            result == WINDOWS_FOCUS_REFRESH_HELD,
            observed_new_event);
        if (rearm) {
          events_schedule_focus_refresh(false);
        }
      }
    });
  }
}

#ifdef DEBUG
static void dump_event(void* data, size_t data_length) {
  if (!data) return;
  for (size_t i = 0; i < data_length; i++) {
    printf("%02x ", *((unsigned char*)data + i));
  }
  printf("\n");
}

static void event_watcher(uint32_t event, void* data, size_t data_length, void* context) {
  static int count = 0;
  printf("(%d) Event: %d; Payload:\n", ++count, event);
  dump_event(data, data_length);
}
#endif

struct window_spawn_data {
  uint64_t sid;
  uint32_t wid;
};

static bool is_own_window(int cid, uint32_t wid) {
  int wid_cid = 0;
  SLSGetWindowOwner(cid, wid, &wid_cid);
  pid_t pid = 0;
  SLSConnectionGetPID(wid_cid, &pid);
  return pid == g_pid;
}

static void window_spawn_handler(uint32_t event,
                                 void* data,
                                 size_t data_length,
                                 int cid) {
  if (!data || data_length < sizeof(struct window_spawn_data)) return;

  struct window_spawn_data spawn_data;
  memcpy(&spawn_data, data, sizeof(spawn_data));
  struct table* windows = &g_windows;
  uint32_t wid = spawn_data.wid;
  uint64_t sid = spawn_data.sid;

  if (!wid || !sid || is_own_window(cid, wid)) return;

  if (event == EVENT_WINDOW_CREATE) {
    if (windows_window_create(windows, wid, sid)) {
      debug("Window Created: %d %d\n", wid, sid);
      events_schedule_space_refresh();
    }
    events_schedule_focus_refresh(true);
  } else if (event == EVENT_WINDOW_DESTROY) {
    if (windows_window_destroy(windows, wid, sid)) {
      debug("Window Destroyed: %d %d\n", wid, sid);
    }
    events_schedule_focus_refresh(true);
  }
}

static void window_modify_handler(uint32_t event,
                                  void* data,
                                  size_t data_length,
                                  int cid) {
  if (!data || data_length < sizeof(uint32_t)) return;

  uint32_t wid = 0;
  memcpy(&wid, data, sizeof(wid));
  if (!wid) return;
  struct table* windows = &g_windows;

  if (is_own_window(cid, wid)) return;

  if (event == EVENT_WINDOW_MOVE) {
    debug("Window Move: %d\n", wid);
    windows_window_move(windows, wid);
  } else if (event == EVENT_WINDOW_RESIZE) {
    debug("Window Resize: %d\n", wid);
    windows_window_resize(windows, wid);
  } else if (event == EVENT_WINDOW_REORDER) {
    debug("Window Reorder (and focus): %d\n", wid);
    windows_window_update(windows, wid);
    events_schedule_focus_refresh(true);
  } else if (event == EVENT_WINDOW_LEVEL) {
    debug("Window Level: %d\n", wid);
    windows_window_update(windows, wid);
  } else if (event == EVENT_WINDOW_TITLE || event == EVENT_WINDOW_UPDATE) {
    debug("Window Focus\n");
    events_schedule_focus_refresh(false);
  } else if (event == EVENT_WINDOW_UNHIDE) {
    debug("Window Unhide: %d\n", wid);
    windows_window_unhide(windows, wid);
    events_schedule_focus_refresh(true);
  } else if (event == EVENT_WINDOW_HIDE) {
    debug("Window Hide: %d\n", wid);
    windows_window_hide(windows, wid);
    events_schedule_focus_refresh(true);
  } else if (event == EVENT_WINDOW_CLOSE) {
    debug("Window Close: %d\n", wid);
    windows_window_destroy(windows, wid, 0);
    events_schedule_focus_refresh(true);
  }
}

static void front_app_handler() {
  debug("Window Focus\n");
  events_schedule_focus_refresh(true);
}

static volatile uint64_t space_change_generation;

void events_schedule_space_refresh(void) {
  // Native-fullscreen transitions publish their window and Space changes at
  // different times, and a newly created helper may not yet be registered when
  // its first move is submitted. Retry a bounded number of consistency scans,
  // cancelling the older series when a newer refresh begins. The blocks only
  // retain a generation value and the process-lifetime window table, never a
  // border pointer that could have been destroyed before the delay expires.
  events_cancel_focus_refresh();
  uint64_t generation = __sync_add_and_fetch(&space_change_generation, 1);

  for (size_t i = 0; i < SPACE_CHANGE_RETRY_COUNT; ++i) {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                                 space_change_retry_delays_us[i]
                                 * NSEC_PER_USEC),
                   dispatch_get_main_queue(), ^{
      if (!space_change_retry_is_current(
              __sync_fetch_and_add(&space_change_generation, 0),
              generation)) {
        return;
      }
      windows_refresh_after_space_change(
          &g_windows,
          i == SPACE_CHANGE_RETRY_COUNT - 1);
      if (i == SPACE_CHANGE_RETRY_COUNT - 1) {
        space_transition_active = false;
      }
    });
  }
}

static void space_handler() {
  space_transition_active = true;
  windows_adaptive_space_change_started(&g_windows);
  events_schedule_space_refresh();
}

void events_register(int cid) {
  void* cid_ctx = (void*)(intptr_t)cid;

  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_CLOSE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_MOVE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_RESIZE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_LEVEL, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_UNHIDE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_HIDE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_TITLE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_REORDER, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_UPDATE, cid_ctx);
  SLSRegisterNotifyProc(window_spawn_handler, EVENT_WINDOW_CREATE, cid_ctx);
  SLSRegisterNotifyProc(window_spawn_handler, EVENT_WINDOW_DESTROY, cid_ctx);

  SLSRegisterNotifyProc(space_handler, EVENT_SPACE_CHANGE, cid_ctx);

  SLSRegisterNotifyProc(front_app_handler, EVENT_FRONT_CHANGE, cid_ctx);

#ifdef DEBUG
  for (int i = 0; i < 2000; i++) {
    SLSRegisterNotifyProc(event_watcher, i, NULL);
  }
#endif
}
