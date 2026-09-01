#pragma once
#include <CoreGraphics/CoreGraphics.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

enum border_position {
  BORDER_POSITION_AUTO,
  BORDER_POSITION_INSIDE,
  BORDER_POSITION_CENTER,
  BORDER_POSITION_OUTSIDE,
};

static inline bool border_position_parse(const char* token,
                                         enum border_position* position) {
  if (strcmp(token, "auto") == 0) {
    *position = BORDER_POSITION_AUTO;
  } else if (strcmp(token, "inside") == 0) {
    *position = BORDER_POSITION_INSIDE;
  } else if (strcmp(token, "center") == 0) {
    *position = BORDER_POSITION_CENTER;
  } else if (strcmp(token, "outside") == 0) {
    *position = BORDER_POSITION_OUTSIDE;
  } else {
    return false;
  }
  return true;
}

enum border_inset_edge {
  BORDER_INSET_LEFT   = 1 << 0,
  BORDER_INSET_RIGHT  = 1 << 1,
  BORDER_INSET_TOP    = 1 << 2,
  BORDER_INSET_BOTTOM = 1 << 3,
};

struct border_geometry {
  CGRect frame;
  CGRect drawing_bounds;
  CGRect path_bounds;
  CGRect clip_bounds;
  unsigned int inset_edges;
  bool force_above;
};

struct border_corner_radii {
  CGFloat top_left;
  CGFloat top_right;
  CGFloat bottom_right;
  CGFloat bottom_left;
};

static inline CGFloat border_geometry_corner_radius(CGFloat base_radius,
                                                     CGFloat horizontal_inset,
                                                     CGFloat vertical_inset) {
  CGFloat inset = horizontal_inset > vertical_inset
                  ? horizontal_inset
                  : vertical_inset;
  CGFloat radius = base_radius - inset;
  return radius > 0.0 ? radius : 0.0;
}

static inline struct border_corner_radii border_geometry_corner_radii(
    CGRect outer,
    CGRect inner,
    CGFloat base_radius) {
  CGFloat left = CGRectGetMinX(inner) - CGRectGetMinX(outer);
  CGFloat right = CGRectGetMaxX(outer) - CGRectGetMaxX(inner);
  CGFloat top = CGRectGetMinY(inner) - CGRectGetMinY(outer);
  CGFloat bottom = CGRectGetMaxY(outer) - CGRectGetMaxY(inner);

  return (struct border_corner_radii) {
    .top_left = border_geometry_corner_radius(base_radius, left, top),
    .top_right = border_geometry_corner_radius(base_radius, right, top),
    .bottom_right = border_geometry_corner_radius(base_radius, right, bottom),
    .bottom_left = border_geometry_corner_radius(base_radius, left, bottom),
  };
}

static inline double border_geometry_intersection_area(CGRect a, CGRect b) {
  CGRect intersection = CGRectIntersection(a, b);
  if (CGRectIsNull(intersection) || CGRectIsEmpty(intersection)) return 0.0;
  return intersection.size.width * intersection.size.height;
}

static inline CGRect border_geometry_select_display(CGRect window_frame,
                                                     const CGRect* displays,
                                                     size_t display_count) {
  CGRect selected = CGRectNull;
  double selected_area = 0.0;

  for (size_t i = 0; i < display_count; ++i) {
    double area = border_geometry_intersection_area(window_frame, displays[i]);
    if (area > selected_area) {
      selected = displays[i];
      selected_area = area;
    }
  }

  return selected;
}

static inline CGRect border_geometry_inset_edges(CGRect rect,
                                                  CGFloat left,
                                                  CGFloat right,
                                                  CGFloat top,
                                                  CGFloat bottom) {
  rect.origin.x += left;
  rect.origin.y += top;
  rect.size.width -= left + right;
  rect.size.height -= top + bottom;
  return rect;
}

static inline struct border_geometry border_geometry_calculate(
    CGRect window_frame,
    CGRect display_frame,
    CGFloat border_width,
    CGFloat padding,
    enum border_position position) {
  CGFloat margin = border_width + padding;
  CGFloat half_width = border_width / 2.0;
  struct border_geometry geometry = { 0 };

  geometry.frame = CGRectInset(window_frame, -margin, -margin);
  geometry.drawing_bounds = (CGRect) {
    .origin = { margin, margin },
    .size = window_frame.size,
  };
  geometry.path_bounds = geometry.drawing_bounds;
  geometry.clip_bounds = CGRectInset(geometry.drawing_bounds, 1.0, 1.0);

  if (position == BORDER_POSITION_OUTSIDE) {
    geometry.path_bounds = CGRectInset(geometry.drawing_bounds,
                                       -half_width,
                                       -half_width);
    geometry.clip_bounds = geometry.drawing_bounds;
  } else if (position == BORDER_POSITION_CENTER) {
    geometry.clip_bounds = CGRectInset(geometry.drawing_bounds,
                                       half_width,
                                       half_width);
  } else if (position == BORDER_POSITION_INSIDE) {
    geometry.path_bounds = CGRectInset(geometry.drawing_bounds,
                                       half_width,
                                       half_width);
    geometry.clip_bounds = CGRectInset(geometry.drawing_bounds,
                                       border_width,
                                       border_width);
    geometry.force_above = true;
  } else if (!CGRectIsNull(display_frame) && !CGRectIsEmpty(display_frame)) {
    CGFloat left = 0.0;
    CGFloat right = 0.0;
    CGFloat top = 0.0;
    CGFloat bottom = 0.0;
    CGFloat clip_left = 1.0;
    CGFloat clip_right = 1.0;
    CGFloat clip_top = 1.0;
    CGFloat clip_bottom = 1.0;

    if (CGRectGetMinX(window_frame) - half_width
        < CGRectGetMinX(display_frame)) {
      geometry.inset_edges |= BORDER_INSET_LEFT;
      left = half_width;
      clip_left = border_width;
    }
    if (CGRectGetMaxX(window_frame) + half_width
        > CGRectGetMaxX(display_frame)) {
      geometry.inset_edges |= BORDER_INSET_RIGHT;
      right = half_width;
      clip_right = border_width;
    }
    if (CGRectGetMinY(window_frame) - half_width
        < CGRectGetMinY(display_frame)) {
      geometry.inset_edges |= BORDER_INSET_TOP;
      top = half_width;
      clip_top = border_width;
    }
    if (CGRectGetMaxY(window_frame) + half_width
        > CGRectGetMaxY(display_frame)) {
      geometry.inset_edges |= BORDER_INSET_BOTTOM;
      bottom = half_width;
      clip_bottom = border_width;
    }

    geometry.path_bounds = border_geometry_inset_edges(
        geometry.drawing_bounds, left, right, top, bottom);
    geometry.clip_bounds = border_geometry_inset_edges(
        geometry.drawing_bounds,
        clip_left,
        clip_right,
        clip_top,
        clip_bottom);
    geometry.force_above = geometry.inset_edges != 0;
  }

  geometry.frame.origin = CGPointZero;
  return geometry;
}
