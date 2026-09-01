#pragma once

#include <stdbool.h>

enum screen_capture_permission_status {
  SCREEN_CAPTURE_PERMISSION_UNSUPPORTED = 0,
  SCREEN_CAPTURE_PERMISSION_DENIED,
  SCREEN_CAPTURE_PERMISSION_GRANTED,
};

// Status is a read-only preflight. Only the explicitly named request helper
// may display macOS permission UI.
enum screen_capture_permission_status screen_capture_permission_status(void);
bool screen_capture_permission_allowed(void);
bool screen_capture_permission_request(void);
