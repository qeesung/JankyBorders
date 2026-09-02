#pragma once
#include <pthread.h>
#include "misc/helpers.h"
#include "misc/window.h"
#include "misc/drawing.h"
#include "geometry.h"
#include "space_recovery.h"
#include "animation.h"
#include "adaptive_color.h"
#include "hashtable.h"

#define BORDER_ORDER_ABOVE 1
#define BORDER_ORDER_BELOW -1
#define BORDER_STYLE_ROUND  'r'
#define BORDER_STYLE_ROUND_UNIFORM 'u'
#define BORDER_STYLE_SQUARE 's'
#define BORDER_STYLE_NONE 'n'
#define BORDER_PADDING 8.0

struct color_style {
  enum { COLOR_STYLE_GRADIENT, COLOR_STYLE_SOLID } stype;
  bool glow;
  union {
    uint32_t colors[BORDER_SIDE_COUNT];
    struct gradient gradient;
  };
};

enum adaptive_color_mode {
  ADAPTIVE_COLOR_MODE_OFF = 0,
  ADAPTIVE_COLOR_MODE_ACTIVE,
  ADAPTIVE_COLOR_MODE_FOCUS,
};

static inline bool adaptive_color_mode_is_enabled(
    enum adaptive_color_mode mode) {
  return mode == ADAPTIVE_COLOR_MODE_ACTIVE
         || mode == ADAPTIVE_COLOR_MODE_FOCUS;
}

enum adaptive_color_mode_transition {
  ADAPTIVE_COLOR_TRANSITION_NONE = 0,
  ADAPTIVE_COLOR_TRANSITION_RESET,
  ADAPTIVE_COLOR_TRANSITION_RESET_AND_REFRESH,
};

static inline enum adaptive_color_mode_transition
adaptive_color_mode_transition_for(enum adaptive_color_mode previous,
                                   enum adaptive_color_mode current) {
  if (previous == current) return ADAPTIVE_COLOR_TRANSITION_NONE;
  return adaptive_color_mode_is_enabled(current)
         ? ADAPTIVE_COLOR_TRANSITION_RESET_AND_REFRESH
         : ADAPTIVE_COLOR_TRANSITION_RESET;
}

struct settings {
  bool enabled;
  uint32_t apply_to;

  struct color_style active_window;
  struct color_style inactive_window;
  struct color_style corner_mask;
  struct color_style background;

  float border_width;
  float blur_radius;
  char border_style;
  enum border_position border_position;
  bool hidpi;
  bool show_background;
  int border_order;
  bool ax_focus;
  bool active_only;
  enum adaptive_color_mode adaptive_color;
};

// Application filters have process-wide scope. Keep their owning tables out of
// struct settings, which is intentionally shallow-copied for window overrides.
extern bool g_blacklist_enabled;
extern struct table g_blacklist;
extern bool g_whitelist_enabled;
extern struct table g_whitelist;

struct event_buffer {
  bool disable_coalescing;
  volatile bool is_coalescing;
  int64_t last_coalesce_attempt;
};

struct border {
  pthread_mutex_t mutex;
  int cid;

  bool focused;
  bool needs_redraw;
  bool too_small;
  bool sticky;

  uint64_t sid;
  uint32_t wid;
  uint32_t target_wid;

  float radius;
  float inner_radius;

  CGPoint origin;
  CGRect frame;
  CGRect target_bounds;
  CGRect drawing_bounds;
  CGRect path_bounds;
  CGRect clip_bounds;
  unsigned int inset_edges;
  int effective_order;
  CGContextRef context;

  struct animation animation;
  struct event_buffer event_buffer;

  bool is_proxy;
  bool destroying;
  struct border* proxy;
  volatile uint32_t external_proxy_wid;

  // Adaptive captures never retain a border pointer. The process-wide
  // generation lets their main-queue callbacks reject destroyed or reused
  // WindowServer IDs before touching this cache.
  uint64_t adaptive_generation;
  struct adaptive_color_cache adaptive_color_cache;

  struct settings setting_override;
};

struct border* border_create();
bool border_init(struct border* border, int cid);
void border_destroy(struct border* border);

void border_move(struct border* border);
void border_retry_space_migration(struct border* border);
void border_update(struct border* border, bool try_async);
void border_hide(struct border* border);
void border_unhide(struct border* border);
void border_adaptive_fallback_colors(
    struct border* border,
    uint32_t colors[ADAPTIVE_COLOR_SIDE_COUNT]);

struct settings* border_get_settings(struct border* border);
