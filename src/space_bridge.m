#import <Foundation/Foundation.h>
#import <objc/message.h>
#include "space_bridge.h"

static bool space_bridge_perform(id operation) {
  if (!operation) return false;

  SEL perform_selector = NSSelectorFromString(@"performWithWMBridgeDelegate");
  SEL release_selector = NSSelectorFromString(@"release");
  if (![operation respondsToSelector:perform_selector]) {
    ((void (*)(id, SEL))objc_msgSend)(operation, release_selector);
    return false;
  }

  @try {
    ((void (*)(id, SEL))objc_msgSend)(operation, perform_selector);
  } @catch (__unused NSException* exception) {
    ((void (*)(id, SEL))objc_msgSend)(operation, release_selector);
    return false;
  }
  ((void (*)(id, SEL))objc_msgSend)(operation, release_selector);
  return true;
}

static bool space_bridge_add_window(NSArray* windows, uint64_t sid) {
  Class operation_class = NSClassFromString(
      @"SLSBridgedSpaceAddWindowsAndRemoveFromSpacesOperation");
  if (!operation_class) return false;

  SEL alloc_selector = NSSelectorFromString(@"alloc");
  SEL init_selector = NSSelectorFromString(
      @"initWithSpaceID:windows:options:");
  SEL release_selector = NSSelectorFromString(@"release");
  id allocated = ((id (*)(id, SEL))objc_msgSend)(operation_class,
                                                 alloc_selector);
  if (!allocated || ![allocated respondsToSelector:init_selector]) {
    if (allocated) {
      ((void (*)(id, SEL))objc_msgSend)(allocated, release_selector);
    }
    return false;
  }

  id operation = allocated;
  @try {
    operation = ((id (*)(id, SEL, uint64_t, NSArray*, uint32_t))objc_msgSend)(
        allocated,
        init_selector,
        sid,
        windows,
        0x80007U);
  } @catch (__unused NSException* exception) {
    ((void (*)(id, SEL))objc_msgSend)(allocated, release_selector);
    return false;
  }
  return space_bridge_perform(operation);
}

static bool space_bridge_move_managed_window(NSArray* windows, uint64_t sid) {
  Class operation_class = NSClassFromString(
      @"SLSBridgedMoveWindowsToManagedSpaceOperation");
  if (!operation_class) return false;

  SEL alloc_selector = NSSelectorFromString(@"alloc");
  SEL init_selector = NSSelectorFromString(@"initWithWindows:spaceID:");
  SEL release_selector = NSSelectorFromString(@"release");
  id allocated = ((id (*)(id, SEL))objc_msgSend)(operation_class,
                                                 alloc_selector);
  if (!allocated || ![allocated respondsToSelector:init_selector]) {
    if (allocated) {
      ((void (*)(id, SEL))objc_msgSend)(allocated, release_selector);
    }
    return false;
  }

  id operation = allocated;
  @try {
    operation = ((id (*)(id, SEL, NSArray*, uint64_t))objc_msgSend)(
        allocated,
        init_selector,
        windows,
        sid);
  } @catch (__unused NSException* exception) {
    ((void (*)(id, SEL))objc_msgSend)(allocated, release_selector);
    return false;
  }
  return space_bridge_perform(operation);
}

bool space_bridge_move_window(uint32_t wid, uint64_t sid) {
  @autoreleasepool {
    NSArray* windows = @[ @(wid) ];
    if (@available(macOS 26.0, *)) {
      if (space_bridge_add_window(windows, sid)) return true;
    }
    return space_bridge_move_managed_window(windows, sid);
  }
}
