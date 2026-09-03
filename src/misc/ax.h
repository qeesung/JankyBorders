#pragma once
#include "extern.h"
#include "helpers.h"

extern void _AXUIElementGetWindow(CFTypeRef window, uint32_t* wid);
static bool g_ax_trust = false;

static inline bool ax_check_trust(bool silent) {
  if (silent) return AXIsProcessTrusted();
  CFStringRef key =  kAXTrustedCheckOptionPrompt;
  CFDictionaryRef dict = CFDictionaryCreate(kCFAllocatorDefault, (const void**)&key, (const void**)&kCFBooleanTrue, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  bool trusted = AXIsProcessTrustedWithOptions(dict);
  CFRelease(dict);

  g_ax_trust = trusted;
  return trusted;
}

static inline uint32_t ax_get_front_window(int cid) {
  if (!g_ax_trust && !ax_check_trust(false)) {
    error("In order to use 'ax_focus=on', the process must be trusted with"
          " accessibility permissions.\n");
    return 0;
  }

  ProcessSerialNumber psn = { 0 };
  if (_SLPSGetFrontProcess(&psn) != noErr) return 0;
  int target_cid = 0;
  if (SLSGetConnectionIDForPSN(cid, &psn, &target_cid)
          != kCGErrorSuccess
      || target_cid <= 0) {
    return 0;
  }

  pid_t pid = 0;
  if (SLSConnectionGetPID(target_cid, &pid) != kCGErrorSuccess || pid <= 0) {
    return 0;
  }

  AXUIElementRef app = AXUIElementCreateApplication(pid);
  if (!app) return 0;
  CFTypeRef window = NULL;
  AXError copy_error = AXUIElementCopyAttributeValue(
      app,
      kAXFocusedWindowAttribute,
      &window);
  CFRelease(app);
  if (copy_error != kAXErrorSuccess || !window) {
    if (window) CFRelease(window);
    return 0;
  }
  uint32_t wid = 0;
  _AXUIElementGetWindow(window, &wid);
  CFRelease(window);
  return wid;
}
