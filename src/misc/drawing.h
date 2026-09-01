#pragma once
#include <CoreGraphics/CoreGraphics.h>

#define BORDER_SIDE_OVERLAP 0.25f
#define BORDER_SIDE_OUTSET 1000.0f

enum border_side {
  BORDER_SIDE_TOP,
  BORDER_SIDE_RIGHT,
  BORDER_SIDE_BOTTOM,
  BORDER_SIDE_LEFT,
  BORDER_SIDE_COUNT
};

struct gradient {
  enum { TL_TO_BR, TR_TO_BL } direction;
  uint32_t color1;
  uint32_t color2;
};

static inline void colors_from_hex(uint32_t hex, float* a, float* r, float* g, float* b) {
  *a = ((hex >> 24) & 0xff) / 255.f;
  *r = ((hex >> 16) & 0xff) / 255.f;
  *g = ((hex >> 8) & 0xff) / 255.f;
  *b = ((hex >> 0) & 0xff) / 255.f;
}

static inline void colors_mix(uint32_t color1,
                              uint32_t color2,
                              float* a,
                              float* r,
                              float* g,
                              float* b) {
  float a1, r1, g1, b1;
  float a2, r2, g2, b2;
  colors_from_hex(color1, &a1, &r1, &g1, &b1);
  colors_from_hex(color2, &a2, &r2, &g2, &b2);

  float alpha_sum = a1 + a2;
  *a = alpha_sum / 2.0f;
  if (alpha_sum > 0.0f) {
    *r = (r1 * a1 + r2 * a2) / alpha_sum;
    *g = (g1 * a1 + g2 * a2) / alpha_sum;
    *b = (b1 * a1 + b2 * a2) / alpha_sum;
  } else {
    *r = 0.0f;
    *g = 0.0f;
    *b = 0.0f;
  }
}

static inline void drawing_set_fill(CGContextRef context, uint32_t color) {
  float a,r,g,b;
  colors_from_hex(color, &a, &r, &g, &b);
  CGContextSetRGBFillColor(context, r, g, b, a);
}

static inline void drawing_set_stroke(CGContextRef context, uint32_t color) {
  float a,r,g,b;
  colors_from_hex(color, &a, &r, &g, &b);
  CGContextSetRGBStrokeColor(context, r, g, b, a);
}

static inline void drawing_set_stroke_and_fill(CGContextRef context, uint32_t color, bool glow) {
  float a,r,g,b;
  colors_from_hex(color, &a, &r, &g, &b);
  CGContextSetRGBFillColor(context, r, g, b, a);
  CGContextSetRGBStrokeColor(context, r, g, b, a);

  if (glow) {
    CGColorRef color_ref = CGColorCreateGenericRGB(r, g, b, 1.0);
    if (color_ref) {
      CGContextSetShadowWithColor(context, CGSizeZero, 10.0, color_ref);
      CGColorRelease(color_ref);
    }
  }
}

static inline void drawing_clip_between_rect_and_path(CGContextRef context, CGRect frame, CGPathRef path) {
  CGMutablePathRef clip_path = CGPathCreateMutable();
  if (!clip_path) return;
  CGPathAddRect(clip_path, NULL, frame);
  CGPathAddPath(clip_path, NULL, path);
  CGContextAddPath(context, clip_path);
  CGContextEOClip(context);
  CFRelease(clip_path);
}

static inline void drawing_add_rect_with_inset(CGContextRef context, CGRect rect, float inset) {
  CGRect square_rect = CGRectInset(rect, inset, inset);
  CGPathRef square_path = CGPathCreateWithRect(square_rect, NULL);
  if (!square_path) return;
  CGContextAddPath(context, square_path);
  CFRelease(square_path);
}

static inline void drawing_add_rounded_rect(CGContextRef context, CGRect rect, float border_radius) {
  CGPathRef stroke_path = CGPathCreateWithRoundedRect(rect,
                                                      border_radius,
                                                      border_radius,
                                                      NULL          );
  if (!stroke_path) return;

  CGContextAddPath(context, stroke_path);
  CFRelease(stroke_path);
}

static inline CGPathRef drawing_create_rect_path(CGRect rect, float inset) {
  return CGPathCreateWithRect(CGRectInset(rect, inset, inset), NULL);
}

static inline CGPathRef drawing_create_rounded_rect_path(CGRect rect, float border_radius) {
  return CGPathCreateWithRoundedRect(rect,
                                     border_radius,
                                     border_radius,
                                     NULL          );
}

static inline void drawing_clip_to_side(CGContextRef context,
                                        CGRect rect,
                                        enum border_side side) {
  bool horizontal = side == BORDER_SIDE_TOP || side == BORDER_SIDE_BOTTOM;
  float half_depth = 0.5f * (horizontal ? rect.size.height : rect.size.width);
  float half_length = 0.5f * (horizontal ? rect.size.width : rect.size.height)
                      + BORDER_SIDE_OVERLAP;
  float depth = fminf(half_depth, half_length);

  CGPoint region[] = {
    { -(half_length + BORDER_SIDE_OUTSET),
        half_depth + BORDER_SIDE_OUTSET },
    {  (half_length + BORDER_SIDE_OUTSET),
        half_depth + BORDER_SIDE_OUTSET },
    {  (half_length - depth), half_depth - depth },
    { -(half_length - depth), half_depth - depth }
  };
  CGAffineTransform transform =
      CGAffineTransformRotate(CGAffineTransformMakeTranslation(
                                  CGRectGetMidX(rect),
                                  CGRectGetMidY(rect)),
                              -M_PI_2 * side);
  CGMutablePathRef path = CGPathCreateMutable();
  CGPathAddLines(path, &transform, region, 4);
  CGPathCloseSubpath(path);
  CGContextAddPath(context, path);
  CGContextClip(context);
  CFRelease(path);
}

static inline bool colors_are_uniform(const uint32_t colors[BORDER_SIDE_COUNT]) {
  return colors[0] == colors[1]
         && colors[0] == colors[2]
         && colors[0] == colors[3];
}

static inline void drawing_paint_path(CGContextRef context,
                                      CGPathRef path,
                                      CGRect rect,
                                      const uint32_t colors[BORDER_SIDE_COUNT],
                                      bool glow,
                                      bool fill) {
  bool per_side = colors && !colors_are_uniform(colors);
  int pass_count = per_side ? BORDER_SIDE_COUNT : 1;
  for (int side = 0; side < pass_count; side++) {
    if (colors && !(colors[side] & 0xff000000)) continue;

    CGContextSaveGState(context);
    if (per_side) drawing_clip_to_side(context, rect, side);
    if (colors) drawing_set_stroke_and_fill(context, colors[side], glow);
    CGContextAddPath(context, path);
    if (fill) CGContextFillPath(context);
    else CGContextStrokePath(context);
    CGContextRestoreGState(context);
  }
}

static inline void drawing_draw_square_with_inset(
    CGContextRef context,
    CGRect rect,
    float inset,
    const uint32_t colors[BORDER_SIDE_COUNT],
    bool glow) {
  CGPathRef path = drawing_create_rect_path(rect, inset);
  drawing_paint_path(context, path, rect, colors, glow, true);
  CFRelease(path);
}

static inline void drawing_draw_square_gradient_with_inset(CGContextRef context,CGGradientRef gradient, CGPoint dir[2], CGRect rect, float inset) {
  drawing_add_rect_with_inset(context, rect, inset);
  CGContextClip(context);
  CGContextDrawLinearGradient(context, gradient, dir[0], dir[1], 0);
}

static inline void drawing_draw_rounded_rect_with_inset(
    CGContextRef context,
    CGRect rect,
    float border_radius,
    bool fill,
    const uint32_t colors[BORDER_SIDE_COUNT],
    bool glow) {
  CGPathRef path = drawing_create_rounded_rect_path(rect, border_radius);
  drawing_paint_path(context, path, rect, colors, glow, fill);
  CFRelease(path);
}

static inline void drawing_draw_rounded_gradient_with_inset(CGContextRef context,CGGradientRef gradient, CGPoint dir[2], CGRect rect, float border_radius) {
  drawing_add_rounded_rect(context, rect, border_radius);
  CGContextReplacePathWithStrokedPath(context);
  CGContextClip(context);
  CGContextDrawLinearGradient(context, gradient, dir[0], dir[1], 0);
}

static inline void drawing_draw_filled_path(CGContextRef context, CGPathRef path, uint32_t color) {
  drawing_set_fill(context, color);
  drawing_set_stroke(context, 0);
  CGContextAddPath(context, path);
  CGContextFillPath(context);
}

static inline CGGradientRef drawing_create_gradient(const struct gradient* gradient, CGAffineTransform trans, CGPoint direction[2]) {
  float a1, a2, r1, r2, g1, g2, b1, b2;
  colors_from_hex(gradient->color1, &a1, &r1, &g1, &b1);
  colors_from_hex(gradient->color2, &a2, &r2, &g2, &b2);
  CGColorRef c[] = { CGColorCreateSRGB(r1, g1, b1, a1),
                     CGColorCreateSRGB(r2, g2, b2, a2) };
  if (!c[0] || !c[1]) {
    if (c[0]) CGColorRelease(c[0]);
    if (c[1]) CGColorRelease(c[1]);
    return NULL;
  }
  CFArrayRef cfc = CFArrayCreate(NULL,
                                 (const void **)c,
                                 2,
                                 &kCFTypeArrayCallBacks);
  if (!cfc) {
    CGColorRelease(c[0]);
    CGColorRelease(c[1]);
    return NULL;
  }
  CGGradientRef result = CGGradientCreateWithColors(NULL, cfc, NULL);
  CFRelease(cfc);
  CGColorRelease(c[0]);
  CGColorRelease(c[1]);
  if (gradient->direction == TR_TO_BL) {
    direction[0] = CGPointMake(1, 1);
    direction[1] = CGPointZero;
  } else if (gradient->direction == TL_TO_BR) {
    direction[0] = CGPointMake(0, 1);
    direction[1] = CGPointMake(1, 0);
  }
  direction[0] = CGPointApplyAffineTransform(direction[0], trans);
  direction[1] = CGPointApplyAffineTransform(direction[1], trans);
  return result;
}
