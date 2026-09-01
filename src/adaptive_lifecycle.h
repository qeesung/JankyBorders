#pragma once

#include <stdbool.h>
#include <stdint.h>

struct adaptive_capture_token {
  uint32_t wid;
  uint64_t generation;
};

struct adaptive_pending_capture {
  bool valid;
  struct adaptive_capture_token token;
  uint64_t serial;
  uint64_t ready_at_ns;
  unsigned int retry_count;
};

static inline uint64_t adaptive_lifecycle_next_nonzero(uint64_t value) {
  ++value;
  return value ? value : UINT64_C(1);
}

static inline void adaptive_pending_replace(
    struct adaptive_pending_capture* pending,
    struct adaptive_capture_token token,
    uint64_t serial,
    uint64_t ready_at_ns,
    unsigned int retry_count) {
  if (!pending) return;
  pending->valid = true;
  pending->token = token;
  pending->serial = serial;
  pending->ready_at_ns = ready_at_ns;
  pending->retry_count = retry_count;
}

static inline void adaptive_pending_cancel_window(
    struct adaptive_pending_capture* pending,
    uint32_t wid) {
  if (pending && pending->valid && pending->token.wid == wid) {
    pending->valid = false;
  }
}

static inline bool adaptive_capture_token_matches(
    struct adaptive_capture_token token,
    uint32_t border_wid,
    uint64_t border_generation,
    bool mode_enabled,
    bool focused,
    bool destroying) {
  return mode_enabled
         && focused
         && !destroying
         && token.wid != 0
         && token.wid == border_wid
         && token.generation != 0
         && token.generation == border_generation;
}
