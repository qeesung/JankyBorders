#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "../src/active_only.h"

static void test_parse_argument(void) {
  bool enabled = false;

  assert(active_only_parse_argument("active_only=on", &enabled));
  assert(enabled);

  assert(active_only_parse_argument("active_only=off", &enabled));
  assert(!enabled);

  assert(!active_only_parse_argument("active_only=true", &enabled));
  assert(!enabled);
  assert(!active_only_parse_argument("active_only=on-extra", &enabled));
  assert(!enabled);
  assert(!active_only_parse_argument(NULL, &enabled));
  assert(!active_only_parse_argument("active_only=on", NULL));
}

static void test_window_tracking_policy(void) {
  assert(active_only_should_track_window(false, 12, 34));
  assert(active_only_should_track_window(true, 12, 12));
  assert(!active_only_should_track_window(true, 12, 34));

  assert(!active_only_should_remove_window(false, 12, 34));
  assert(!active_only_should_remove_window(true, 12, 12));
  assert(active_only_should_remove_window(true, 12, 34));
  assert(active_only_should_remove_window(true, 12, 0));
}

int main(void) {
  test_parse_argument();
  test_window_tracking_policy();
  return 0;
}
