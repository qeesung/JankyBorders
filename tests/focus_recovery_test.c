#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include "../src/focus_recovery.h"

static void test_retry_plan_is_bounded_and_increasing(void) {
  static const uint64_t expected_delays_us[] = {
    10000,
    60000,
    160000,
    350000,
  };

  assert(FOCUS_RECOVERY_RETRY_COUNT == 4);
  assert(sizeof(expected_delays_us) / sizeof(expected_delays_us[0])
         == FOCUS_RECOVERY_RETRY_COUNT);
  for (size_t i = 0; i < FOCUS_RECOVERY_RETRY_COUNT; ++i) {
    assert(focus_recovery_retry_delays_us[i] == expected_delays_us[i]);
    if (i > 0) {
      assert(focus_recovery_retry_delays_us[i]
             > focus_recovery_retry_delays_us[i - 1]);
    }
  }
}

static void test_stale_retry_generations_are_cancelled(void) {
  assert(focus_recovery_retry_is_current(12, 12));
  assert(!focus_recovery_retry_is_current(13, 12));
  assert(!focus_recovery_retry_is_current(12, 13));
}

static void test_frontmost_selection_ignores_candidate_space_order(void) {
  const uint32_t candidates[] = { 41, 73, 52 };
  const uint32_t ordered_windows[] = { 900, 73, 41, 52 };
  assert(focus_recovery_select_frontmost(
             ordered_windows,
             sizeof(ordered_windows) / sizeof(ordered_windows[0]),
             candidates,
             sizeof(candidates) / sizeof(candidates[0])) == 73);
}

static void test_frontmost_selection_requires_a_visible_candidate(void) {
  const uint32_t candidates[] = { 41, 73 };
  const uint32_t ordered_windows[] = { 900, 901 };
  assert(focus_recovery_select_frontmost(
             ordered_windows,
             sizeof(ordered_windows) / sizeof(ordered_windows[0]),
             candidates,
             sizeof(candidates) / sizeof(candidates[0])) == 0);
  assert(focus_recovery_select_frontmost(NULL, 0, candidates, 2) == 0);
}

static void test_event_bursts_coalesce_without_postponing_active_series(void) {
  struct focus_recovery_scheduler scheduler = { 0 };
  assert(focus_recovery_scheduler_schedule(&scheduler));
  for (int i = 0; i < 1000; ++i) {
    assert(!focus_recovery_scheduler_schedule(&scheduler));
  }
  assert(scheduler.active);
  assert(focus_recovery_scheduler_observe_events(&scheduler));
  assert(!focus_recovery_scheduler_observe_events(&scheduler));
  assert(!focus_recovery_scheduler_finish(&scheduler, false, true));
  assert(!scheduler.active);
  assert(focus_recovery_scheduler_schedule(&scheduler));
}

static void test_held_dirty_final_probe_requests_one_trailing_series(void) {
  struct focus_recovery_scheduler scheduler = { 0 };
  assert(focus_recovery_scheduler_schedule(&scheduler));
  assert(!focus_recovery_scheduler_schedule(&scheduler));
  bool observed = focus_recovery_scheduler_observe_events(&scheduler);
  assert(focus_recovery_scheduler_finish(&scheduler, true, observed));
  assert(focus_recovery_scheduler_schedule(&scheduler));
  focus_recovery_scheduler_cancel(&scheduler);
  assert(!scheduler.active);
  assert(!scheduler.dirty);
}

static void test_fallback_requires_adjacent_matching_snapshots(void) {
  struct focus_recovery_fallback_snapshot snapshot = { 0 };
  assert(!focus_recovery_fallback_observe(&snapshot, 12, 41));
  assert(focus_recovery_fallback_observe(&snapshot, 12, 41));

  focus_recovery_fallback_reset(&snapshot);
  assert(!focus_recovery_fallback_observe(&snapshot, 12, 41));
  // A probe without a trustworthy hint must break the A -> A sequence.
  assert(!focus_recovery_fallback_observe(&snapshot, 0, 0));
  assert(!focus_recovery_fallback_observe(&snapshot, 12, 41));
  assert(focus_recovery_fallback_observe(&snapshot, 12, 41));

  focus_recovery_fallback_reset(&snapshot);
  assert(!focus_recovery_fallback_observe(&snapshot, 12, 41));
  assert(!focus_recovery_fallback_observe(&snapshot, 12, 73));
  assert(focus_recovery_fallback_observe(&snapshot, 12, 73));
}

static void test_resolved_window_is_selected(void) {
  assert(focus_recovery_decide(FOCUS_RECOVERY_RESULT_RESOLVED, true)
         == FOCUS_RECOVERY_SELECT);
  assert(focus_recovery_decide(FOCUS_RECOVERY_RESULT_RESOLVED, false)
         == FOCUS_RECOVERY_SELECT);
}

static void test_only_final_attempt_can_clear_or_reject_old_process(void) {
  assert(focus_recovery_gate_clear(
             FOCUS_RECOVERY_RESULT_COMPLETE_NONE,
             false) == FOCUS_RECOVERY_RESULT_TRANSIENT);
  assert(focus_recovery_gate_clear(
             FOCUS_RECOVERY_RESULT_AMBIGUOUS,
             false) == FOCUS_RECOVERY_RESULT_TRANSIENT);
  assert(focus_recovery_gate_clear(
             FOCUS_RECOVERY_RESULT_COMPLETE_NONE,
             true) == FOCUS_RECOVERY_RESULT_COMPLETE_NONE);
  assert(focus_recovery_gate_clear(
             FOCUS_RECOVERY_RESULT_RESOLVED,
             false) == FOCUS_RECOVERY_RESULT_RESOLVED);
}

static void test_transient_snapshot_holds_confirmed_border(void) {
  assert(focus_recovery_decide(FOCUS_RECOVERY_RESULT_TRANSIENT, true)
         == FOCUS_RECOVERY_HOLD);
  assert(focus_recovery_decide(FOCUS_RECOVERY_RESULT_TRANSIENT, false)
         == FOCUS_RECOVERY_HOLD);
}

static void test_complete_empty_snapshot_clears_border(void) {
  assert(focus_recovery_decide(FOCUS_RECOVERY_RESULT_COMPLETE_NONE, true)
         == FOCUS_RECOVERY_CLEAR);
  assert(focus_recovery_decide(FOCUS_RECOVERY_RESULT_COMPLETE_NONE, false)
         == FOCUS_RECOVERY_CLEAR);
}

static void test_ambiguous_snapshot_only_holds_same_front_process(void) {
  assert(focus_recovery_decide(FOCUS_RECOVERY_RESULT_AMBIGUOUS, true)
         == FOCUS_RECOVERY_HOLD);
  assert(focus_recovery_decide(FOCUS_RECOVERY_RESULT_AMBIGUOUS, false)
         == FOCUS_RECOVERY_CLEAR);
}

int main(void) {
  test_retry_plan_is_bounded_and_increasing();
  test_stale_retry_generations_are_cancelled();
  test_frontmost_selection_ignores_candidate_space_order();
  test_frontmost_selection_requires_a_visible_candidate();
  test_event_bursts_coalesce_without_postponing_active_series();
  test_held_dirty_final_probe_requests_one_trailing_series();
  test_fallback_requires_adjacent_matching_snapshots();
  test_resolved_window_is_selected();
  test_only_final_attempt_can_clear_or_reject_old_process();
  test_transient_snapshot_holds_confirmed_border();
  test_complete_empty_snapshot_clears_border();
  test_ambiguous_snapshot_only_holds_same_front_process();
  puts("focus recovery tests passed");
  return 0;
}
