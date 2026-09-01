#pragma once

#include <stdbool.h>
#include <stdint.h>

#define WINDOW_TAG_DOCUMENT      (1ULL << 0)
#define WINDOW_TAG_FLOATING      (1ULL << 1)
#define WINDOW_TAG_ATTACHED      (1ULL << 7)
#define WINDOW_TAG_STICKY        (1ULL << 11)
#define WINDOW_TAG_IGNORES_CYCLE (1ULL << 18)
#define WINDOW_TAG_MODAL         (1ULL << 31)

static inline bool window_policy_is_suitable(uint64_t tags,
                                             uint64_t attributes,
                                             uint32_t parent_wid) {
  if (parent_wid != 0
      || !((attributes & 0x2) || (tags & 0x400000000000000))
      || (tags & WINDOW_TAG_ATTACHED)) {
    return false;
  }

  // Some normal application windows (including Mail, iTerm and Ghostty
  // variants) opt out of system window cycling. Keep real document windows,
  // while using that tag to reject transient floating/modal UI such as IDE
  // tooltips and oversized quick-add overlays.
  if (tags & WINDOW_TAG_DOCUMENT) return true;
  return (tags & WINDOW_TAG_FLOATING)
         && (tags & WINDOW_TAG_MODAL)
         && !(tags & WINDOW_TAG_IGNORES_CYCLE);
}
