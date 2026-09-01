#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "adaptive_color.h"
#include "screen_capture.h"

enum edge_sampler_status {
  EDGE_SAMPLER_OK = 0,
  EDGE_SAMPLER_UNSUPPORTED,
  EDGE_SAMPLER_CAPTURE_PERMISSION_DENIED,
  EDGE_SAMPLER_WINDOW_NOT_FOUND,
  EDGE_SAMPLER_CONTENT_UNAVAILABLE,
  EDGE_SAMPLER_CAPTURE_FAILED,
  EDGE_SAMPLER_INVALID_IMAGE,
  EDGE_SAMPLER_ANALYSIS_FAILED,
};

struct edge_sampler_frame {
  const uint8_t* pixels;
  size_t pixels_size;
  size_t width;
  size_t height;
  size_t bytes_per_row;
};

struct edge_sampler_result {
  enum edge_sampler_status status;
  uint32_t wid;
  uint64_t generation;

  // pixels is non-NULL only when a frame was captured successfully. The
  // storage belongs to edge_sampler and is valid only for the callback.
  struct edge_sampler_frame frame;

  bool has_analysis;
  enum adaptive_color_status analysis_status;
  struct adaptive_color_result analysis;
};

typedef void (*edge_sampler_callback)(const struct edge_sampler_result* result,
                                      void* context);

// Captures a fixed-size BGRA/SDR frame for wid and analyzes its four edges.
// previous and fallback are copied before this function returns, so callers do
// not need to keep them alive. callback is always delivered on the main queue.
// The caller owns context and must keep it alive until callback returns.
void edge_sampler_capture(uint32_t wid,
                          uint64_t generation,
                          const struct adaptive_color_cache* previous,
                          const uint32_t fallback[ADAPTIVE_COLOR_SIDE_COUNT],
                          edge_sampler_callback callback,
                          void* context);

const char* edge_sampler_status_string(enum edge_sampler_status status);
