#include <assert.h>

#include "../src/window_policy.h"

int main(void) {
  uint64_t required_attribute = 0x2;

  assert(window_policy_is_suitable(WINDOW_TAG_DOCUMENT,
                                   required_attribute,
                                   0));
  assert(window_policy_is_suitable(WINDOW_TAG_DOCUMENT
                                   | WINDOW_TAG_IGNORES_CYCLE,
                                   required_attribute,
                                   0));
  assert(window_policy_is_suitable(WINDOW_TAG_FLOATING | WINDOW_TAG_MODAL,
                                   required_attribute,
                                   0));
  assert(!window_policy_is_suitable(WINDOW_TAG_FLOATING
                                    | WINDOW_TAG_MODAL
                                    | WINDOW_TAG_IGNORES_CYCLE,
                                    required_attribute,
                                    0));
  assert(!window_policy_is_suitable(WINDOW_TAG_DOCUMENT
                                    | WINDOW_TAG_ATTACHED,
                                    required_attribute,
                                    0));
  assert(!window_policy_is_suitable(WINDOW_TAG_DOCUMENT,
                                    required_attribute,
                                    42));
  assert(!window_policy_is_suitable(WINDOW_TAG_DOCUMENT, 0, 0));
  return 0;
}
