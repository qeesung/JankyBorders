#include "events.h"
#include "misc/extern.h"
#include "windows.h"
#include "border.h"
#include "misc/window.h"
#include "space_recovery.h"
#include <string.h>

extern struct table g_windows;
extern pid_t g_pid;

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
      windows_determine_and_focus_active_window(windows);
    }
  } else if (event == EVENT_WINDOW_DESTROY) {
    if (windows_window_destroy(windows, wid, sid)) {
      debug("Window Destroyed: %d %d\n", wid, sid);
    }
    windows_determine_and_focus_active_window(windows);
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
    windows_window_update(windows, wid);
  } else if (event == EVENT_WINDOW_REORDER) {
    debug("Window Reorder (and focus): %d\n", wid);
    windows_window_update(windows, wid);
    DELAY_ASYNC_EXEC_ON_MAIN_THREAD(10000, {
      windows_determine_and_focus_active_window(windows);
    });
  } else if (event == EVENT_WINDOW_LEVEL) {
    debug("Window Level: %d\n", wid);
    windows_window_update(windows, wid);
  } else if (event == EVENT_WINDOW_TITLE || event == EVENT_WINDOW_UPDATE) {
    debug("Window Focus\n");
    DELAY_ASYNC_EXEC_ON_MAIN_THREAD(50000, {
      windows_determine_and_focus_active_window(windows);
    });
  } else if (event == EVENT_WINDOW_UNHIDE) {
    debug("Window Unhide: %d\n", wid);
    windows_window_unhide(windows, wid);
  } else if (event == EVENT_WINDOW_HIDE) {
    debug("Window Hide: %d\n", wid);
    windows_window_hide(windows, wid);
  } else if (event == EVENT_WINDOW_CLOSE) {
    debug("Window Close: %d\n", wid);
    windows_window_destroy(windows, wid, 0);
  }
}

static void front_app_handler() {
  debug("Window Focus\n");
  DELAY_ASYNC_EXEC_ON_MAIN_THREAD(50000, {
    windows_determine_and_focus_active_window(&g_windows);
  });
}

static volatile uint64_t space_change_generation;

void events_schedule_space_refresh(void) {
  // Native-fullscreen transitions publish their window and Space changes at
  // different times, and a newly created helper may not yet be registered when
  // its first move is submitted. Retry a bounded number of consistency scans,
  // cancelling the older series when a newer refresh begins. The blocks only
  // retain a generation value and the process-lifetime window table, never a
  // border pointer that could have been destroyed before the delay expires.
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
      windows_draw_borders_on_current_spaces(&g_windows);
      windows_determine_and_focus_active_window(&g_windows);
    });
  }
}

static void space_handler() {
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
