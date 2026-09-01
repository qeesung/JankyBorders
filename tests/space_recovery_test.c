#include <assert.h>
#include <stdio.h>
#include "../src/space_recovery.h"

static void test_retry_plan_is_bounded_and_increasing(void) {
  assert(SPACE_CHANGE_RETRY_COUNT == 5);
  assert(space_change_retry_delays_us[0] == 20000);
  assert(space_change_retry_delays_us[SPACE_CHANGE_RETRY_COUNT - 1]
         == 1000000);
  for (size_t i = 1; i < SPACE_CHANGE_RETRY_COUNT; ++i) {
    assert(space_change_retry_delays_us[i]
           > space_change_retry_delays_us[i - 1]);
  }
}

static void test_stale_retry_generations_are_cancelled(void) {
  assert(space_change_retry_is_current(7, 7));
  assert(!space_change_retry_is_current(8, 7));
}

static void test_helper_migrates_on_sid_change_and_bounded_retry(void) {
  assert(border_space_should_migrate(10, 11, 10, false));
  assert(!border_space_should_migrate(11, 11, 10, false));
  assert(border_space_should_migrate(11, 11, 10, true));
  assert(!border_space_should_migrate(11, 11, 11, true));
  assert(!border_space_should_migrate(11, 0, 11, true));
}

int main(void) {
  test_retry_plan_is_bounded_and_increasing();
  test_stale_retry_generations_are_cancelled();
  test_helper_migrates_on_sid_change_and_bounded_retry();
  puts("space recovery tests passed");
  return 0;
}
