#include "border.h"
#include "hashtable.h"
#include "events.h"
#include "misc/extern.h"
#include "windows.h"
#include "mach.h"
#include "parse.h"
#include "misc/connection.h"
#include "misc/ax.h"
#include "misc/yabai.h"
#include "screen_capture.h"
#include <stdio.h>
#include <dlfcn.h>

#define VERSION_OPT_LONG "--version"
#define VERSION_OPT_SHRT "-v"

#define HELP_OPT_LONG "--help"
#define HELP_OPT_SHRT "-h"

#define SCREEN_CAPTURE_STATUS_OPT "--screen-capture-status"
#define SCREEN_CAPTURE_REQUEST_OPT "--request-screen-capture"

#define MAJOR 1
#define MINOR 9
#define PATCH 0

// Resolved via dlsym because of availability
CFArrayRef (* JBSLSWindowIteratorGetCornerRadii)(CFTypeRef) = NULL;

pid_t g_pid;
mach_port_t g_server_port;
struct table g_windows;
struct mach_server g_mach_server;
struct settings g_settings = { .enabled = true,
                               .active_window = { .stype = COLOR_STYLE_SOLID,
                                                  .colors = { 0xffe1e3e4,
                                                              0xffe1e3e4,
                                                              0xffe1e3e4,
                                                              0xffe1e3e4 } },
                               .inactive_window = { .stype = COLOR_STYLE_SOLID,
                                                    .colors = { 0x00000000,
                                                                0x00000000,
                                                                0x00000000,
                                                                0x00000000 } },
                               .background = { .stype = COLOR_STYLE_SOLID,
                                               .colors = { 0x00000000,
                                                           0x00000000,
                                                           0x00000000,
                                                           0x00000000 } },
                               .border_width = 4.f,
                               .blur_radius = 0,
                               .border_style = BORDER_STYLE_ROUND,
                               .border_position = BORDER_POSITION_AUTO,
                               .hidpi = false,
                               .show_background = false,
                               .border_order = BORDER_ORDER_BELOW,
                               .ax_focus = false,
                               .active_only = false,
                               .adaptive_color = ADAPTIVE_COLOR_MODE_OFF     };

bool g_blacklist_enabled = false;
struct table g_blacklist;
bool g_whitelist_enabled = false;
struct table g_whitelist;

static TABLE_HASH_FUNC(hash_windows) {
  return *(uint32_t *) key;
}

static TABLE_COMPARE_FUNC(cmp_windows) {
  return *(uint32_t *) key_a == *(uint32_t *) key_b;
}

static TABLE_HASH_FUNC(hash_blacklist) {
  // djb2 by Dan Bernstein
  unsigned long hash = 5381;
  char c;
  while((c = *((char*)key++))) {
    hash = ((hash << 5) + hash) + c;
  }
  return hash;
}

static TABLE_COMPARE_FUNC(cmp_blacklist) {
  return strcmp((char*)key_a, (char*)key_b) == 0;
}

static void message_handler(void* data, uint32_t len) {
  char** arguments = NULL;
  int argument_count = 0;
  if (!mach_decode_arguments(data, len, &arguments, &argument_count)) return;

  if (!parse_settings_scope_is_valid(argument_count, arguments)) {
    printf("[?] Borders: process-wide settings cannot be applied to one window\n");
    free(arguments);
    return;
  }

  uint32_t update_mask = 0;
  enum adaptive_color_mode previous_adaptive_mode = g_settings.adaptive_color;
  struct settings settings = g_settings;
  update_mask = parse_settings(&settings, argument_count, arguments);

  if (settings.apply_to > 0) {
    struct border* border = table_find(&g_windows, &settings.apply_to);
    if (border) {
      border->setting_override = settings;
      border->setting_override.enabled = true;
      border->needs_redraw = true;
      border_update(border, true);
    }
    free(arguments);
    return;
  } else {
    g_settings = settings;
    for (int i = 0; i < g_windows.capacity; ++i) {
      struct bucket* bucket = g_windows.buckets[i];
      while (bucket) {
        if (bucket->value) {
          struct border* border = bucket->value;
          if (border->setting_override.enabled) {
            uint32_t window_update_mask
              = parse_settings_override(&border->setting_override,
                                        argument_count,
                                        arguments                   );

            if (window_update_mask
                && !((update_mask & BORDER_UPDATE_MASK_ALL)
                     || (update_mask & BORDER_UPDATE_MASK_RECREATE_ALL))) {
              border->needs_redraw = true;
              border_update(border, true);
            }
          }
        }
        bucket = bucket->next;
      }
    }
  }

  free(arguments);

  if (update_mask & BORDER_UPDATE_MASK_ADAPTIVE) {
    windows_adaptive_mode_changed(&g_windows,
                                  previous_adaptive_mode,
                                  g_settings.adaptive_color);
  }

  if (update_mask & BORDER_UPDATE_MASK_RECREATE_ALL) {
    windows_recreate_all_borders(&g_windows);
    // Helper creation and Space assignment are asynchronous. Reuse the
    // bounded consistency series so runtime hidpi/active-only/filter changes
    // cannot leave the recreated focused helper waiting for another event.
    events_schedule_space_refresh();
  } else if (update_mask & BORDER_UPDATE_MASK_ALL) {
    windows_update_all(&g_windows);
  } else if (update_mask & BORDER_UPDATE_MASK_ACTIVE) {
    windows_update_active(&g_windows);
  } else if (update_mask & BORDER_UPDATE_MASK_INACTIVE) {
    windows_update_inactive(&g_windows);
  }
}

static int screen_capture_command(const char* option) {
  enum screen_capture_permission_status status =
      screen_capture_permission_status();
  if (strcmp(option, SCREEN_CAPTURE_STATUS_OPT) == 0) {
    const char* value = status == SCREEN_CAPTURE_PERMISSION_GRANTED
                        ? "granted"
                        : status == SCREEN_CAPTURE_PERMISSION_DENIED
                          ? "denied"
                          : "unsupported";
    fprintf(stdout, "Screen capture permission: %s\n", value);
    return EXIT_SUCCESS;
  }

  if (status == SCREEN_CAPTURE_PERMISSION_UNSUPPORTED) {
    fprintf(stderr, "[!] Borders: Screen capture is unsupported on this macOS version\n");
    return EXIT_FAILURE;
  }
  if (status != SCREEN_CAPTURE_PERMISSION_GRANTED) {
    (void)screen_capture_permission_request();
    status = screen_capture_permission_status();
  }
  if (status == SCREEN_CAPTURE_PERMISSION_GRANTED) {
    fprintf(stdout, "Screen capture permission: granted\n");
  } else {
    fprintf(stdout,
            "Screen capture permission is not granted yet. Enable JankyBorders "
            "in System Settings > Privacy & Security > Screen & System Audio "
            "Recording, then run 'make service-restart'.\n");
  }
  return EXIT_SUCCESS;
}

static bool send_args_to_server(mach_port_t port, int argc, char** argv) {
  void* message = NULL;
  uint32_t message_length = 0;
  if (!mach_encode_arguments(argc - 1,
                             argv + 1,
                             &message,
                             &message_length)) {
    return false;
  }

  bool sent = mach_send_message(port, message, message_length);
  free(message);
  return sent;
}

static void event_callback(CFMachPortRef port, void* message, CFIndex size, void* context) {
  (void)port;
  (void)message;
  (void)size;
  (void)context;
  int cid = SLSMainConnectionID();
  CGEventRef event = SLEventCreateNextEvent(cid);
  if (!event) return;
  do {
    CFRelease(event);
    event = SLEventCreateNextEvent(cid);
  } while (event);
}

void load_symbols() {
  if (__builtin_available(macOS 26.0, *)) {
    void* lib = dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight", RTLD_LAZY | RTLD_LOCAL);
    if (lib) {
      JBSLSWindowIteratorGetCornerRadii = dlsym(lib, "SLSWindowIteratorGetCornerRadii");
    }
  }
}

int main(int argc, char** argv) {
  if (argc > 1 && ((strcmp(argv[1], VERSION_OPT_LONG) == 0)
                   || (strcmp(argv[1], VERSION_OPT_SHRT) == 0))) {
    fprintf(stdout, "borders-v%d.%d.%d\n", MAJOR, MINOR, PATCH);
    exit(EXIT_SUCCESS);
  }

  if (argc > 1 && ((strcmp(argv[1], HELP_OPT_LONG) == 0)
                   || (strcmp(argv[1], HELP_OPT_SHRT) == 0))) {
    fprintf(stdout, "Refer to the man page for help: man borders\n");
    exit(EXIT_SUCCESS);
  }

  // Permission utilities must remain usable without starting an instance or
  // prompting for Accessibility access. The LaunchAgent never passes either
  // option, so login cannot trigger the screen-capture prompt.
  if (argc == 2
      && (strcmp(argv[1], SCREEN_CAPTURE_STATUS_OPT) == 0
          || strcmp(argv[1], SCREEN_CAPTURE_REQUEST_OPT) == 0)) {
    return screen_capture_command(argv[1]);
  }

  if (!parse_settings_scope_is_valid(argc - 1, argv + 1)) {
    fprintf(stderr,
            "[?] Borders: process-wide settings cannot be applied to one window\n");
    return EXIT_FAILURE;
  }

  if (!table_init(&g_blacklist, 64, hash_blacklist, cmp_blacklist)
      || !table_init(&g_whitelist, 64, hash_blacklist, cmp_blacklist)) {
    error("[!] Borders: Failed to allocate application filters\n");
  }
  g_settings.ax_focus = ax_check_trust(true);

  uint32_t update_mask = parse_settings(&g_settings, argc - 1, argv + 1);
  mach_port_t server_port = mach_get_bs_port(BS_NAME);
  if (server_port) {
    bool sent = update_mask && send_args_to_server(server_port, argc, argv);
    mach_port_deallocate(mach_task_self(), server_port);
    if (update_mask) return sent ? EXIT_SUCCESS : EXIT_FAILURE;
    error("A borders instance is already running and no valid arguments"
          " where provided. To modify properties of the running instance"
          " provide them as arguments.\n");
  }

  load_symbols();
  pid_for_task(mach_task_self(), &g_pid);
  if (!table_init(&g_windows, 1024, hash_windows, cmp_windows)) {
    error("[!] Borders: Failed to allocate window table\n");
  }

  g_server_port = create_connection_server_port();

  int cid = SLSMainConnectionID();
  events_register(cid);

  mach_port_t port;
  CGError err = SLSGetEventPort(cid, &port);
  if (err == kCGErrorSuccess) {
    CFMachPortRef cf_mach_port = CFMachPortCreateWithPort(NULL,
                                                          port,
                                                          event_callback,
                                                          NULL,
                                                          false          );
    if (cf_mach_port) {
      _CFMachPortSetOptions(cf_mach_port, 0x40);
      CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(NULL,
                                                                cf_mach_port,
                                                                0            );
      if (source) {
        CFRunLoopAddSource(CFRunLoopGetCurrent(),
                           source,
                           kCFRunLoopDefaultMode);
        CFRelease(source);
      }
      CFRelease(cf_mach_port);
    }
  }

  windows_add_existing_windows(&g_windows);
  events_schedule_space_refresh();

  if (!mach_server_begin(&g_mach_server, message_handler)) {
    error("[!] Borders: Failed to start command server\n");
  }
  if (!update_mask) execute_config_file("borders", "bordersrc");

  #ifdef _YABAI_INTEGRATION
  yabai_register_mach_port(&g_windows);
  #endif
  CFRunLoopRun();
  return 0;
}
