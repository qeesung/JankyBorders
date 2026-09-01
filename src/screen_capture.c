#include "screen_capture.h"

#include <CoreGraphics/CoreGraphics.h>

enum screen_capture_permission_status screen_capture_permission_status(void) {
  if (__builtin_available(macOS 10.15, *)) {
    return CGPreflightScreenCaptureAccess()
           ? SCREEN_CAPTURE_PERMISSION_GRANTED
           : SCREEN_CAPTURE_PERMISSION_DENIED;
  }
  return SCREEN_CAPTURE_PERMISSION_UNSUPPORTED;
}

bool screen_capture_permission_allowed(void) {
  return screen_capture_permission_status()
         == SCREEN_CAPTURE_PERMISSION_GRANTED;
}

bool screen_capture_permission_request(void) {
  if (__builtin_available(macOS 10.15, *)) {
    return CGRequestScreenCaptureAccess();
  }
  return false;
}
