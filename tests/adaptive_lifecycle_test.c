#include <assert.h>
#include <stdint.h>

#include "../src/adaptive_lifecycle.h"

static void test_newest_pending_request_wins(void) {
  struct adaptive_pending_capture pending = { 0 };
  struct adaptive_capture_token first = { .wid = 10, .generation = 1 };
  struct adaptive_capture_token newest = { .wid = 20, .generation = 2 };

  adaptive_pending_replace(&pending, first, 1, 80, 0);
  adaptive_pending_replace(&pending, newest, 2, 160, 0);

  assert(pending.valid);
  assert(pending.token.wid == 20);
  assert(pending.token.generation == 2);
  assert(pending.serial == 2);
  assert(pending.ready_at_ns == 160);
}

static void test_focus_switch_rejects_old_callback(void) {
  struct adaptive_capture_token window_a = { .wid = 10, .generation = 7 };
  struct adaptive_capture_token window_b = { .wid = 20, .generation = 8 };

  assert(!adaptive_capture_token_matches(window_a, 10, 9, true, false, false));
  assert(adaptive_capture_token_matches(window_b, 20, 8, true, true, false));
}

static void test_palette_switch_rejects_old_callback(void) {
  struct adaptive_capture_token stale = { .wid = 20, .generation = 8 };
  assert(!adaptive_capture_token_matches(stale, 20, 9, true, true, false));
}

static void test_reused_window_id_needs_new_generation(void) {
  struct adaptive_capture_token stale = { .wid = 42, .generation = 3 };
  assert(!adaptive_capture_token_matches(stale, 42, 4, true, true, false));
}

static void test_disabled_hidden_and_destroying_are_rejected(void) {
  struct adaptive_capture_token token = { .wid = 42, .generation = 3 };
  assert(!adaptive_capture_token_matches(token, 42, 3, false, true, false));
  assert(!adaptive_capture_token_matches(token, 42, 3, true, false, false));
  assert(!adaptive_capture_token_matches(token, 42, 3, true, true, true));
}

static void test_cancel_and_generation_wrap(void) {
  struct adaptive_pending_capture pending = { 0 };
  struct adaptive_capture_token token = { .wid = 42, .generation = 3 };
  adaptive_pending_replace(&pending, token, 1, 0, 0);
  adaptive_pending_cancel_window(&pending, 41);
  assert(pending.valid);
  adaptive_pending_cancel_window(&pending, 42);
  assert(!pending.valid);

  assert(adaptive_lifecycle_next_nonzero(0) == 1);
  assert(adaptive_lifecycle_next_nonzero(UINT64_MAX) == 1);
}

int main(void) {
  test_newest_pending_request_wins();
  test_focus_switch_rejects_old_callback();
  test_palette_switch_rejects_old_callback();
  test_reused_window_id_needs_new_generation();
  test_disabled_hidden_and_destroying_are_rejected();
  test_cancel_and_generation_wrap();
  return 0;
}
