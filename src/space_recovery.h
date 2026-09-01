#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { SPACE_CHANGE_RETRY_COUNT = 5 };

static const uint64_t space_change_retry_delays_us[
    SPACE_CHANGE_RETRY_COUNT] = {
  20000,
  100000,
  250000,
  500000,
  1000000,
};

static inline bool space_change_retry_is_current(uint64_t current_generation,
                                                 uint64_t scheduled_generation) {
  return current_generation == scheduled_generation;
}

static inline bool border_space_should_migrate(uint64_t previous_target_sid,
                                               uint64_t target_sid,
                                               uint64_t helper_sid,
                                               bool retry) {
  return target_sid != 0
         && helper_sid != target_sid
         && (target_sid != previous_target_sid || retry);
}
