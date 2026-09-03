#pragma once
#include <stdlib.h>
#include "border.h"
#include "hashtable.h"

void windows_update_inactive(struct table* windows);
void windows_update_active(struct table* windows);
void windows_update_all(struct table* windows);
void windows_update_notifications(struct table* windows);

void windows_window_update(struct table* windows, uint32_t wid);
void windows_window_resize(struct table* windows, uint32_t wid);
void windows_window_hide(struct table* windows, uint32_t wid);
void windows_window_unhide(struct table* windows, uint32_t wid);
void windows_window_move(struct table* windows, uint32_t wid);
bool windows_window_create(struct table* windows, uint32_t wid, uint64_t sid);
bool windows_window_destroy(struct table* windows, uint32_t wid, uint32_t sid);

void windows_add_existing_windows(struct table* windows);
void windows_draw_borders_on_current_spaces(struct table* windows);
void windows_refresh_after_space_change(struct table* windows,
                                        bool final_retry);
enum windows_focus_refresh_result {
  WINDOWS_FOCUS_REFRESH_APPLIED,
  WINDOWS_FOCUS_REFRESH_HELD,
  WINDOWS_FOCUS_REFRESH_CLEARED,
};
void windows_focus_probe_reset(void);
enum windows_focus_refresh_result windows_refresh_active_window(
    struct table* windows,
    bool allow_clear);
void windows_recreate_all_borders(struct table* windows);
void windows_adaptive_mode_changed(struct table* windows,
                                   enum adaptive_color_mode previous,
                                   enum adaptive_color_mode current);
void windows_adaptive_refresh_active(struct table* windows);
void windows_adaptive_space_change_started(struct table* windows);
