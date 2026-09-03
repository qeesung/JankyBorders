#import <AppKit/AppKit.h>
#include <dispatch/dispatch.h>

#include "events.h"
#include "workspace_events.h"

@interface JBWorkspaceActivationObserver : NSObject
@end

@implementation JBWorkspaceActivationObserver
- (void)applicationDidActivate:(NSNotification*)notification {
  (void)notification;
  dispatch_async(dispatch_get_main_queue(), ^{
    events_workspace_did_activate();
  });
}
@end

void workspace_events_register(void) {
  // Process-lifetime observer: NSWorkspace is a public, event-driven fallback
  // for private WindowServer front-change notifications that can be missed
  // while focus and the active display are being published independently.
  static JBWorkspaceActivationObserver* observer;
  static dispatch_once_t once_token;

  dispatch_once(&once_token, ^{
    @autoreleasepool {
      observer = [[JBWorkspaceActivationObserver alloc] init];
      [[[NSWorkspace sharedWorkspace] notificationCenter]
          addObserver:observer
             selector:@selector(applicationDidActivate:)
                 name:NSWorkspaceDidActivateApplicationNotification
               object:nil];
    }
  });
}
