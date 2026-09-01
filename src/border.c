#include "border.h"
#include "hashtable.h"
#include "misc/extern.h"
#include "windows.h"
#include <pthread.h>
#include <time.h>

extern struct settings g_settings;

struct settings* border_get_settings(struct border* border) {
  assert(pthread_main_np() != 0);
  return border->setting_override.enabled
         ? &border->setting_override
         : &g_settings;
}

static void border_destroy_window(struct border* border) {
  if (border->context) CGContextRelease(border->context);
  if (border->wid) SLSReleaseWindow(border->cid, border->wid);
  border->wid = 0;
  border->context = NULL;
}

static bool border_check_too_small(struct border* border, CGRect window_frame) {
  CGRect smallest_rect = CGRectInset(window_frame, 1.0, 1.0);
  if (smallest_rect.size.width < 2.f * border->inner_radius
      || smallest_rect.size.height < 2.f * border->inner_radius) {
    return true;
  }
  return false;
}

static CGRect border_display_frame(CGRect window_frame) {
  enum { BORDER_MAX_ACTIVE_DISPLAYS = 32 };
  CGDirectDisplayID display_ids[BORDER_MAX_ACTIVE_DISPLAYS];
  uint32_t display_count = 0;
  if (CGGetActiveDisplayList(BORDER_MAX_ACTIVE_DISPLAYS,
                             display_ids,
                             &display_count) != kCGErrorSuccess
      || display_count == 0) {
    return CGRectNull;
  }

  CGRect display_frames[BORDER_MAX_ACTIVE_DISPLAYS];
  for (uint32_t i = 0; i < display_count; ++i) {
    display_frames[i] = CGDisplayBounds(display_ids[i]);
  }

  return border_geometry_select_display(window_frame,
                                        display_frames,
                                        display_count);
}

static bool border_calculate_bounds(struct border* border, CGRect* frame, struct settings* settings) {
  CGRect window_frame;
  if (border->is_proxy) window_frame = border->target_bounds;
  else SLSGetWindowBounds(border->cid, border->target_wid, &window_frame);

  border->target_bounds = window_frame;
  CGRect display_frame = settings->border_position == BORDER_POSITION_AUTO
                         ? border_display_frame(window_frame)
                         : CGRectNull;
  struct border_geometry geometry = border_geometry_calculate(
      window_frame,
      display_frame,
      settings->border_width,
      BORDER_PADDING,
      settings->border_position);

  border->too_small = border_check_too_small(border, window_frame)
                      || CGRectIsEmpty(geometry.path_bounds)
                      || CGRectIsEmpty(geometry.clip_bounds);
  if (border->too_small) {
    border_hide(border);
    return false;
  }

  int effective_order = geometry.force_above
                        ? BORDER_ORDER_ABOVE
                        : settings->border_order;
  if (!CGRectEqualToRect(border->drawing_bounds, geometry.drawing_bounds)
      || !CGRectEqualToRect(border->path_bounds, geometry.path_bounds)
      || !CGRectEqualToRect(border->clip_bounds, geometry.clip_bounds)
      || border->effective_order != effective_order) {
    border->needs_redraw = true;
  }

  CGFloat margin = settings->border_width + BORDER_PADDING;
  border->origin = (CGPoint) {
    window_frame.origin.x - margin,
    window_frame.origin.y - margin,
  };
  border->drawing_bounds = geometry.drawing_bounds;
  border->path_bounds = geometry.path_bounds;
  border->clip_bounds = geometry.clip_bounds;
  border->inset_edges = geometry.inset_edges;
  border->effective_order = effective_order;
  *frame = geometry.frame;

  return true;
}

static bool border_corner_radii_are_uniform(struct border_corner_radii radii) {
  return radii.top_left == radii.top_right
         && radii.top_left == radii.bottom_right
         && radii.top_left == radii.bottom_left;
}

static bool border_add_rounded_rect(CGMutablePathRef path,
                                    CGRect rect,
                                    struct border_corner_radii radii) {
  if (!path) return false;
  if (border_corner_radii_are_uniform(radii)) {
    CGPathAddRoundedRect(path,
                         NULL,
                         rect,
                         radii.top_left,
                         radii.top_left);
    return true;
  }

  CGFloat min_x = CGRectGetMinX(rect);
  CGFloat max_x = CGRectGetMaxX(rect);
  CGFloat min_y = CGRectGetMinY(rect);
  CGFloat max_y = CGRectGetMaxY(rect);

  CGPathMoveToPoint(path, NULL, min_x + radii.top_left, min_y);
  CGPathAddLineToPoint(path, NULL, max_x - radii.top_right, min_y);
  CGPathAddArcToPoint(path,
                      NULL,
                      max_x,
                      min_y,
                      max_x,
                      min_y + radii.top_right,
                      radii.top_right);
  CGPathAddLineToPoint(path, NULL, max_x, max_y - radii.bottom_right);
  CGPathAddArcToPoint(path,
                      NULL,
                      max_x,
                      max_y,
                      max_x - radii.bottom_right,
                      max_y,
                      radii.bottom_right);
  CGPathAddLineToPoint(path, NULL, min_x + radii.bottom_left, max_y);
  CGPathAddArcToPoint(path,
                      NULL,
                      min_x,
                      max_y,
                      min_x,
                      max_y - radii.bottom_left,
                      radii.bottom_left);
  CGPathAddLineToPoint(path, NULL, min_x, min_y + radii.top_left);
  CGPathAddArcToPoint(path,
                      NULL,
                      min_x,
                      min_y,
                      min_x + radii.top_left,
                      min_y,
                      radii.top_left);
  CGPathCloseSubpath(path);
  return true;
}

static bool border_draw_gradient_glow(CGContextRef context,
                                      const struct gradient* gradient,
                                      CGPathRef path,
                                      float blur_radius,
                                      bool fill) {
  if (!context || !gradient || !path) return false;

  float a, r, g, b;
  colors_mix(gradient->color1, gradient->color2, &a, &r, &g, &b);
  CGColorRef glow_color = CGColorCreateGenericRGB(r, g, b, a);

  CGContextSaveGState(context);
  if (glow_color) {
    CGContextSetShadowWithColor(context, CGSizeZero, blur_radius, glow_color);
    CGColorRelease(glow_color);
  }
  CGContextSetRGBFillColor(context, 1.0f, 1.0f, 1.0f, 1.0f);
  CGContextSetRGBStrokeColor(context, 1.0f, 1.0f, 1.0f, 1.0f);
  CGContextAddPath(context, path);
  if (fill) CGContextFillPath(context);
  else CGContextStrokePath(context);

  CGContextSetShadowWithColor(context, CGSizeZero, 0, NULL);
  CGContextSetBlendMode(context, kCGBlendModeDestinationOut);
  CGContextAddPath(context, path);
  if (fill) CGContextFillPath(context);
  else CGContextStrokePath(context);
  CGContextRestoreGState(context);
  return true;
}

static bool border_draw_gradient_path(CGContextRef context,
                                      CGGradientRef gradient,
                                      CGPoint direction[2],
                                      CGPathRef path,
                                      bool fill) {
  if (!context || !gradient || !direction || !path) return false;
  CGContextAddPath(context, path);
  if (!fill) CGContextReplacePathWithStrokedPath(context);
  CGContextClip(context);
  CGContextDrawLinearGradient(context,
                              gradient,
                              direction[0],
                              direction[1],
                              0);
  return true;
}

static bool border_draw(struct border* border,
                        CGRect frame,
                        struct settings* settings) {
  CGContextSaveGState(border->context);
  border->needs_redraw = false;
  struct color_style color_style = border->focused
                                   ? settings->active_window
                                   : settings->inactive_window;
  const uint32_t* colors = color_style.stype == COLOR_STYLE_SOLID
                           ? color_style.colors
                           : NULL;

  CGGradientRef gradient = NULL;
  CGMutablePathRef inner_clip_path = NULL;
  CGPathRef border_path = NULL;
  CGPoint gradient_dir[2] = { 0 };
  if (color_style.stype == COLOR_STYLE_GRADIENT) {
    CGAffineTransform trans = CGAffineTransformMakeScale(frame.size.width,
                                                         frame.size.height);
    gradient = drawing_create_gradient(&color_style.gradient,
                                       trans,
                                       gradient_dir          );
    if (!gradient) {
      uint32_t fallback_color = color_style.gradient.color1;
      color_style.stype = COLOR_STYLE_SOLID;
      for (int side = 0; side < BORDER_SIDE_COUNT; ++side) {
        color_style.colors[side] = fallback_color;
      }
      colors = color_style.colors;
      drawing_set_stroke_and_fill(border->context,
                                  fallback_color,
                                  false         );
    }
  }

  CGContextSetLineWidth(border->context, settings->border_width);
  CGContextClearRect(border->context, frame);

  CGRect path_rect = border->path_bounds;
  inner_clip_path = CGPathCreateMutable();
  if (!inner_clip_path) goto draw_failed;
  if (settings->border_style == BORDER_STYLE_SQUARE) {
    CGPathAddRect(inner_clip_path, NULL, border->clip_bounds);
  } else {
    struct border_corner_radii clip_radii;
    if (settings->border_position == BORDER_POSITION_AUTO
        && border->inset_edges == 0) {
      clip_radii = (struct border_corner_radii) {
        border->inner_radius,
        border->inner_radius,
        border->inner_radius,
        border->inner_radius,
      };
    } else {
      clip_radii = border_geometry_corner_radii(border->drawing_bounds,
                                                border->clip_bounds,
                                                border->radius);
    }
    if (!border_add_rounded_rect(inner_clip_path,
                                 border->clip_bounds,
                                 clip_radii)) {
      goto draw_failed;
    }
  }
  if (!drawing_clip_between_rect_and_path(border->context,
                                          frame,
                                          inner_clip_path)) {
    goto draw_failed;
  }

  bool square = settings->border_style == BORDER_STYLE_SQUARE;
  float inset = -settings->border_width / 2.f;
  float corner_radius = settings->border_style == BORDER_STYLE_ROUND_UNIFORM
                        ? 9.0
                        : border->radius;

  if (square) {
    border_path = drawing_create_rect_path(path_rect, inset);
  } else {
    struct border_corner_radii path_radii = border_geometry_corner_radii(
        border->drawing_bounds,
        path_rect,
        corner_radius);
    CGMutablePathRef rounded_path = CGPathCreateMutable();
    if (!border_add_rounded_rect(rounded_path, path_rect, path_radii)) {
      if (rounded_path) CFRelease(rounded_path);
      goto draw_failed;
    }
    border_path = rounded_path;
  }
  if (!border_path) goto draw_failed;

  if (color_style.stype == COLOR_STYLE_SOLID) {
    if (!square
        && settings->border_style == BORDER_STYLE_ROUND_UNIFORM
        && !drawing_paint_path(border->context,
                               border_path,
                               path_rect,
                               colors,
                               color_style.glow,
                               true)) {
      goto draw_failed;
    }
    if (!drawing_paint_path(border->context,
                            border_path,
                            path_rect,
                            colors,
                            color_style.glow,
                            square)) {
      goto draw_failed;
    }
  } else if (color_style.stype == COLOR_STYLE_GRADIENT) {
    if (color_style.glow
        && !border_draw_gradient_glow(border->context,
                                      &color_style.gradient,
                                      border_path,
                                      10.0f,
                                      square)) {
      goto draw_failed;
    }

    if (!square
        && settings->border_style == BORDER_STYLE_ROUND_UNIFORM) {
      CGContextSaveGState(border->context);
      bool filled_gradient = border_draw_gradient_path(border->context,
                                                       gradient,
                                                       gradient_dir,
                                                       border_path,
                                                       true);
      CGContextRestoreGState(border->context);
      if (!filled_gradient) goto draw_failed;
    }

    CGContextSaveGState(border->context);
    bool drew_gradient = border_draw_gradient_path(border->context,
                                                   gradient,
                                                   gradient_dir,
                                                   border_path,
                                                   square);
    CGContextRestoreGState(border->context);
    if (!drew_gradient) goto draw_failed;
  }

  if (settings->show_background
      && border->effective_order != BORDER_ORDER_ABOVE) {
    CGContextRestoreGState(border->context);
    CGContextSaveGState(border->context);
    color_style = settings->background;
    if (color_style.stype == COLOR_STYLE_SOLID) {
      drawing_draw_filled_path(border->context,
                               inner_clip_path,
                               color_style.colors[0]);
    }
  }
  CFRelease(inner_clip_path);
  CFRelease(border_path);
  if (gradient) CGGradientRelease(gradient);
  CGContextFlush(border->context);
  CGContextRestoreGState(border->context);
  SLSFlushWindowContentRegion(border->cid, border->wid, NULL);
  SLSWindowThaw(border->cid, border->wid);
  return true;

draw_failed:
  border->needs_redraw = true;
  if (inner_clip_path) CFRelease(inner_clip_path);
  if (border_path) CFRelease(border_path);
  if (gradient) CGGradientRelease(gradient);
  CGContextRestoreGState(border->context);
  SLSWindowThaw(border->cid, border->wid);
  return false;
}

void border_create_window(struct border* border, CGRect frame, bool unmanaged, bool hidpi) {
  pthread_mutex_lock(&border->mutex);
  int cid = border->cid;
  border->wid = window_create(cid, frame, hidpi, unmanaged);
  if (!border->wid) {
    pthread_mutex_unlock(&border->mutex);
    return;
  }

  border->frame = frame;
  border->needs_redraw = true;
  border->context = SLWindowContextCreate(cid, border->wid, NULL);
  if (!border->context) {
    SLSReleaseWindow(cid, border->wid);
    border->wid = 0;
    pthread_mutex_unlock(&border->mutex);
    return;
  }
  CGContextSetInterpolationQuality(border->context, kCGInterpolationNone);

  if (!border->sid) border->sid = window_space_id(cid, border->target_wid);
  window_send_to_space(cid, border->wid, border->sid);
  pthread_mutex_unlock(&border->mutex);
}

static void border_refresh_space(struct border* border, bool retry_helpers) {
  if (border->is_proxy) return;

  uint64_t sid = window_direct_space_id(border->cid, border->target_wid);
  if (!sid) return;

  uint64_t previous_sid = border->sid;
  bool sid_changed = sid != previous_sid;
  border->sid = sid;
  if (border->wid
      && border_space_should_migrate(previous_sid,
                                     sid,
                                     window_direct_space_id(border->cid,
                                                            border->wid),
                                     retry_helpers)) {
    window_recover_to_space(border->cid, border->wid, sid);
  }
  if (border->proxy && border->proxy->wid) {
    uint64_t previous_proxy_sid = border->proxy->sid;
    border->proxy->sid = sid;
    if (border_space_should_migrate(
            previous_proxy_sid,
            sid,
            window_direct_space_id(border->proxy->cid, border->proxy->wid),
            retry_helpers)) {
      window_recover_to_space(border->proxy->cid, border->proxy->wid, sid);
    }
  }
  if (sid_changed) {
    debug("Window %u moved to Space %llu\n", border->target_wid, sid);
  }
}

void border_update_internal(struct border* border, struct settings* settings) {
  border_refresh_space(border, false);
  if (border->external_proxy_wid) return;
  if (settings->border_style == BORDER_STYLE_NONE) {
    border_hide(border);
    return;
  }

  int cid = border->cid;
  CGRect frame;
  if (!border_calculate_bounds(border, &frame, settings)) return;

  uint64_t tags = window_tags(cid, border->target_wid);
  border->sticky = tags & WINDOW_TAG_STICKY;
  if (!border->sticky && !is_space_visible(cid, border->sid)) return;


  bool shown = false;
  SLSWindowIsOrderedIn(cid, border->target_wid, &shown);
  if (!shown && !border->is_proxy) {
    border_hide(border);
    return;
  } 

  int level = window_level(cid, border->target_wid);
  int sub_level = window_sub_level(cid, border->target_wid);

  if (!border->wid) {
    border_create_window(border,
                         frame,
                         border->is_proxy,
                         settings->hidpi  );
    if (!border->wid || !border->context) return;
  }

  bool updates_disabled = false;
  bool window_frozen = false;
  if (!CGRectEqualToRect(frame, border->frame)) {
    if (SLSDisableUpdate(cid) != kCGErrorSuccess) return;
    updates_disabled = true;

    CFTypeRef frame_region = NULL;
    if (CGSNewRegionWithRect(&frame, &frame_region) != kCGErrorSuccess
        || !frame_region) {
      goto update_cleanup;
    }

    if (SLSWindowFreezeWithOptions(border->cid,
                                   border->wid,
                                   NULL) != kCGErrorSuccess) {
      CFRelease(frame_region);
      goto update_cleanup;
    }
    window_frozen = true;
    CGError shape_error = SLSSetWindowShape(border->cid,
                                            border->wid,
                                            border->origin.x,
                                            border->origin.y,
                                            frame_region);
    CFRelease(frame_region);
    if (shape_error != kCGErrorSuccess) goto update_cleanup;

    border->needs_redraw = true;
    border->frame = frame;
  }

  if (border->needs_redraw) {
    bool draw_succeeded = border_draw(border, frame, settings);
    window_frozen = false;
    if (!draw_succeeded) goto update_cleanup;
  }

  CFTypeRef transaction = SLSTransactionCreate(cid);
  if (!transaction) goto update_cleanup;
  SLSTransactionMoveWindowWithGroup(transaction, border->wid, border->origin);

  if (!border->is_proxy) {
    CGAffineTransform transform = CGAffineTransformIdentity;
    transform.tx = -border->origin.x;
    transform.ty = -border->origin.y;
    SLSTransactionSetWindowTransform(transaction,
                                     border->wid,
                                     0,
                                     0,
                                     transform   );
  }
  SLSTransactionSetWindowLevel(transaction, border->wid, level);
  SLSTransactionSetWindowSubLevel(transaction, border->wid, sub_level);
  SLSTransactionOrderWindow(transaction,
                            border->wid,
                            border->effective_order,
                            border->target_wid      );
  CGError transaction_error = SLSTransactionCommit(transaction, 0);
  CFRelease(transaction);
  if (transaction_error != kCGErrorSuccess) goto update_cleanup;

  uint64_t set_tags = (1ULL << 1) | (1ULL << 9);
  uint64_t clear_tags = 0;

  if (border->sticky) {
    set_tags |= WINDOW_TAG_STICKY;
    clear_tags |= (1ULL << 45);
  }

  SLSSetWindowTags(cid, border->wid, &set_tags, 0x40);
  SLSClearWindowTags(cid, border->wid, &clear_tags, 0x40);

update_cleanup:
  if (window_frozen) SLSWindowThaw(cid, border->wid);
  if (updates_disabled) SLSReenableUpdate(cid);
}

void border_init(struct border* border, int cid) {
  memset(border, 0, sizeof(struct border));
  pthread_mutexattr_t mattr;
  pthread_mutexattr_init(&mattr);
  pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&border->mutex, &mattr);
  animation_init(&border->animation);
  if (cid) border->cid = cid;
  else border->cid = SLSMainConnectionID();
}

struct border* border_create() {
  struct border* border = malloc(sizeof(struct border));
  if (!border) return NULL;
  border_init(border, SLSMainConnectionID());
  return border;
}

void border_destroy(struct border* border) {
  border_hide(border);
  dispatch_async(dispatch_get_main_queue(), ^{
    pthread_mutex_lock(&border->mutex);
    border_destroy_window(border);
    if (border->proxy) border_destroy(border->proxy);
    animation_stop(&border->animation);
    if (!border->is_proxy && border->cid != SLSMainConnectionID())
      SLSReleaseConnection(border->cid);
    pthread_mutex_unlock(&border->mutex);
    free(border);
  });
}

void border_move(struct border* border) {
  border_update(border, true);
}

void border_retry_space_migration(struct border* border) {
  pthread_mutex_lock(&border->mutex);
  border_refresh_space(border, true);
  pthread_mutex_unlock(&border->mutex);
}

void border_update(struct border* border, bool try_async) {
  (void)try_async;
  pthread_mutex_lock(&border->mutex);
  struct settings* settings = border_get_settings(border);
  border_update_internal(border, settings);
  pthread_mutex_unlock(&border->mutex);
}

void border_hide(struct border* border) {
  pthread_mutex_lock(&border->mutex);
  if (border->wid) {
    CFTypeRef transaction = SLSTransactionCreate(border->cid);
    if (transaction) {
      SLSTransactionOrderWindow(transaction,
                                border->wid,
                                0,
                                border->target_wid);
      SLSTransactionCommit(transaction, 0);
      CFRelease(transaction);
    }
  }
  pthread_mutex_unlock(&border->mutex);
}

void border_unhide(struct border* border) {
  pthread_mutex_lock(&border->mutex);
  border_refresh_space(border, false);
  struct settings* settings = border_get_settings(border);
  if (settings->border_style == BORDER_STYLE_NONE
      || border->too_small
      || border->external_proxy_wid
      || (!border->sticky && !is_space_visible(border->cid, border->sid))) {
    pthread_mutex_unlock(&border->mutex);
    return;
  }

  if (border->wid) {
    CFTypeRef transaction = SLSTransactionCreate(border->cid);
    if (transaction) {
      SLSTransactionOrderWindow(transaction,
                                border->wid,
                                border->effective_order,
                                border->target_wid      );
      SLSTransactionCommit(transaction, 0);
      CFRelease(transaction);
    }
  }
  pthread_mutex_unlock(&border->mutex);
}
