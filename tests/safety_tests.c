#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "border.h"
#include "hashtable.h"
#include "mach.h"
#include "misc/helpers.h"
#include "parse.h"

bool g_blacklist_enabled = false;
struct table g_blacklist;
bool g_whitelist_enabled = false;
struct table g_whitelist;

static TABLE_HASH_FUNC(hash_string) {
  unsigned long hash = 5381;
  char c;
  while ((c = *((char*)key++))) hash = ((hash << 5) + hash) + c;
  return hash;
}

static TABLE_COMPARE_FUNC(compare_string) {
  return strcmp(key_a, key_b) == 0;
}

static void test_argument_codec(void) {
  char* source[] = { "width=7", "order=above" };
  void* payload = NULL;
  uint32_t payload_size = 0;
  assert(mach_encode_arguments(2, source, &payload, &payload_size));
  assert(payload_size == strlen(source[0]) + strlen(source[1]) + 3);
  assert(((char*)payload)[payload_size - 1] == '\0');
  assert(((char*)payload)[payload_size - 2] == '\0');

  char** decoded = NULL;
  int count = 0;
  assert(mach_decode_arguments(payload, payload_size, &decoded, &count));
  assert(count == 2);
  assert(strcmp(decoded[0], source[0]) == 0);
  assert(strcmp(decoded[1], source[1]) == 0);
  free(decoded);
  free(payload);

  char missing_sentinel[] = { 'x', '\0' };
  char legacy_one_argument[] = { 'x', '\0', '\0', 'y' };
  char invalid_trailing_data[] = { 'x', '\0', '\0', 'y', 'z' };
  char missing_nul[] = { 'x', 'y', 'z' };
  char empty[] = { '\0', '\0', '\0' };
  assert(!mach_decode_arguments(missing_sentinel,
                                sizeof(missing_sentinel),
                                &decoded,
                                &count));
  assert(mach_decode_arguments(legacy_one_argument,
                               sizeof(legacy_one_argument),
                               &decoded,
                               &count));
  assert(count == 1);
  assert(strcmp(decoded[0], "x") == 0);
  free(decoded);
  assert(!mach_decode_arguments(invalid_trailing_data,
                                sizeof(invalid_trailing_data),
                                &decoded,
                                &count));
  assert(!mach_decode_arguments(missing_nul,
                                sizeof(missing_nul),
                                &decoded,
                                &count));
  assert(!mach_decode_arguments(empty, sizeof(empty), &decoded, &count));
  assert(!mach_encode_arguments(0, source, &payload, &payload_size));

  char legacy_two_arguments[] = {
    'w', 'i', 'd', 't', 'h', '=', '7', '\0',
    'o', 'r', 'd', 'e', 'r', '=', 'a', 'b', 'o', 'v', 'e', '\0',
    '\0', (char)0xa5, (char)0x5a
  };
  assert(mach_decode_arguments(legacy_two_arguments,
                               sizeof(legacy_two_arguments),
                               &decoded,
                               &count));
  assert(count == 2);
  assert(strcmp(decoded[0], "width=7") == 0);
  assert(strcmp(decoded[1], "order=above") == 0);
  free(decoded);

  unsigned int state = 0x13579bdfU;
  for (size_t length = 0; length < 512; ++length) {
    char* fuzz = malloc(length ? length : 1);
    assert(fuzz);
    for (size_t i = 0; i < length; ++i) {
      state = state * 1103515245U + 12345U;
      fuzz[i] = (char)(state >> 24);
    }
    decoded = NULL;
    count = 0;
    if (mach_decode_arguments(fuzz, (uint32_t)length, &decoded, &count)) {
      assert(count > 0);
      free(decoded);
    }
    free(fuzz);
  }
}

static void test_outer_mach_validation(void) {
  char payload_data[] = { 'x', '\0', '\0' };
  struct mach_message message = { 0 };
  message.header.msgh_bits = MACH_MSGH_BITS_COMPLEX;
  message.header.msgh_size = sizeof(message);
  message.msgh_descriptor_count = 1;
  message.descriptor.address = payload_data;
  message.descriptor.size = sizeof(payload_data);
  message.descriptor.type = MACH_MSG_OOL_DESCRIPTOR;

  void* payload = NULL;
  uint32_t payload_size = 0;
  assert(mach_message_get_payload(&message,
                                  sizeof(message),
                                  &payload,
                                  &payload_size));
  assert(payload == payload_data);
  assert(payload_size == sizeof(payload_data));

  message.header.msgh_size--;
  assert(!mach_message_get_payload(&message,
                                   sizeof(message),
                                   &payload,
                                   &payload_size));
  message.header.msgh_size = sizeof(message);
  message.msgh_descriptor_count = 2;
  assert(!mach_message_get_payload(&message,
                                   sizeof(message),
                                   &payload,
                                   &payload_size));
  message.msgh_descriptor_count = 1;
  message.descriptor.size = MACH_MESSAGE_MAX_PAYLOAD + 1U;
  assert(mach_message_get_payload(&message,
                                  sizeof(message),
                                  &payload,
                                  &payload_size));
  char minimal[] = { 'x', '\0', '\0' };
  char** oversized_arguments = NULL;
  int oversized_count = 0;
  assert(!mach_decode_arguments(minimal,
                                MACH_MESSAGE_MAX_PAYLOAD + 1U,
                                &oversized_arguments,
                                &oversized_count));
}

static void test_invalid_mach_message_cleanup(void) {
  mach_port_t task = mach_task_self();
  mach_port_t receive_port = MACH_PORT_NULL;
  mach_port_t tracked_port = MACH_PORT_NULL;
  assert(mach_port_allocate(task,
                            MACH_PORT_RIGHT_RECEIVE,
                            &receive_port) == KERN_SUCCESS);
  assert(mach_port_insert_right(task,
                                receive_port,
                                receive_port,
                                MACH_MSG_TYPE_MAKE_SEND) == KERN_SUCCESS);
  assert(mach_port_allocate(task,
                            MACH_PORT_RIGHT_RECEIVE,
                            &tracked_port) == KERN_SUCCESS);
  assert(mach_port_insert_right(task,
                                tracked_port,
                                tracked_port,
                                MACH_MSG_TYPE_MAKE_SEND) == KERN_SUCCESS);

  mach_port_urefs_t initial_refs = 0;
  assert(mach_port_get_refs(task,
                            tracked_port,
                            MACH_PORT_RIGHT_SEND,
                            &initial_refs) == KERN_SUCCESS);

  struct invalid_message {
    mach_msg_header_t header;
    mach_msg_body_t body;
    mach_msg_port_descriptor_t descriptors[2];
  } outgoing = { 0 };
  outgoing.header.msgh_bits = MACH_MSGH_BITS_COMPLEX
                              | MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
  outgoing.header.msgh_size = sizeof(outgoing);
  outgoing.header.msgh_remote_port = receive_port;
  outgoing.body.msgh_descriptor_count = 2;
  for (size_t i = 0; i < 2; ++i) {
    outgoing.descriptors[i].name = tracked_port;
    outgoing.descriptors[i].disposition = MACH_MSG_TYPE_COPY_SEND;
    outgoing.descriptors[i].type = MACH_MSG_PORT_DESCRIPTOR;
  }
  assert(mach_msg(&outgoing.header,
                  MACH_SEND_MSG,
                  sizeof(outgoing),
                  0,
                  MACH_PORT_NULL,
                  MACH_MSG_TIMEOUT_NONE,
                  MACH_PORT_NULL) == KERN_SUCCESS);

  union {
    struct invalid_message message;
    uint8_t storage[sizeof(struct invalid_message)
                    + sizeof(mach_msg_max_trailer_t)];
  } incoming = { 0 };
  assert(mach_msg(&incoming.message.header,
                  MACH_RCV_MSG,
                  0,
                  sizeof(incoming),
                  receive_port,
                  MACH_MSG_TIMEOUT_NONE,
                  MACH_PORT_NULL) == KERN_SUCCESS);

  mach_port_urefs_t received_refs = 0;
  assert(mach_port_get_refs(task,
                            tracked_port,
                            MACH_PORT_RIGHT_SEND,
                            &received_refs) == KERN_SUCCESS);
  assert(received_refs == initial_refs + 2);

  void* payload = NULL;
  uint32_t payload_size = 0;
  assert(!mach_message_get_payload(&incoming.message,
                                   incoming.message.header.msgh_size,
                                   &payload,
                                   &payload_size));
  mach_destroy_received_message(&incoming.message,
                                incoming.message.header.msgh_size);

  mach_port_urefs_t cleaned_refs = 0;
  assert(mach_port_get_refs(task,
                            tracked_port,
                            MACH_PORT_RIGHT_SEND,
                            &cleaned_refs) == KERN_SUCCESS);
  assert(cleaned_refs == initial_refs);
  assert(mach_port_deallocate(task, tracked_port) == KERN_SUCCESS);
  assert(mach_port_mod_refs(task,
                            tracked_port,
                            MACH_PORT_RIGHT_RECEIVE,
                            -1) == KERN_SUCCESS);
  assert(mach_port_deallocate(task, receive_port) == KERN_SUCCESS);
  assert(mach_port_mod_refs(task,
                            receive_port,
                            MACH_PORT_RIGHT_RECEIVE,
                            -1) == KERN_SUCCESS);
}

static void test_filter_scope(void) {
  assert(table_init(&g_blacklist, 4, hash_string, compare_string));
  assert(table_init(&g_whitelist, 4, hash_string, compare_string));

  struct settings settings = { 0 };
  char* filter[] = { "blacklist=Safari,Mail" };
  uint32_t mask = parse_settings(&settings, 1, filter);
  assert(mask & BORDER_UPDATE_MASK_RECREATE_ALL);
  assert(g_blacklist_enabled);
  assert(table_find(&g_blacklist, "Safari"));
  assert(table_find(&g_blacklist, "Mail"));

  char* override_arguments[] = {
    "blacklist=Ghostty", "width=7", "apply-to=42"
  };
  struct settings override = { .border_width = 4.f };
  mask = parse_settings_override(&override, 3, override_arguments);
  assert(mask & BORDER_UPDATE_MASK_ALL);
  assert(override.border_width == 7.f);
  assert(override.apply_to == 0);
  assert(table_find(&g_blacklist, "Safari"));
  assert(!table_find(&g_blacklist, "Ghostty"));
  assert(parse_settings_contains_global_filter(3, override_arguments));
  assert(parse_settings_apply_target(3, override_arguments) == 42);
  assert(!parse_settings_scope_is_valid(3, override_arguments));
  char* global_arguments[] = { "blacklist=Ghostty", "width=7" };
  assert(parse_settings_scope_is_valid(2, global_arguments));

  struct color_style original = {
    .stype = COLOR_STYLE_SOLID,
    .colors = { 0xff123456,
                0xff123456,
                0xff123456,
                0xff123456 }
  };
  settings.active_window = original;
  char* truncated_gradient[] = {
    "active_color=gradient(top_left=0xff000000,bottom_right=)"
  };
  assert(!(parse_settings(&settings, 1, truncated_gradient)
           & BORDER_UPDATE_MASK_ACTIVE));
  assert(settings.active_window.stype == original.stype);
  assert(memcmp(settings.active_window.colors,
                original.colors,
                sizeof(original.colors)) == 0);

  table_free(&g_blacklist);
  table_free(&g_whitelist);
}

static void test_cfnumber_arrays(void) {
  CFArrayRef empty = cfarray_of_cfnumbers(NULL,
                                          sizeof(uint64_t),
                                          0,
                                          kCFNumberSInt64Type);
  assert(empty);
  assert(CFArrayGetCount(empty) == 0);
  CFRelease(empty);

  uint32_t values[] = { 7, 42 };
  CFArrayRef array = cfarray_of_cfnumbers(values,
                                          sizeof(values[0]),
                                          2,
                                          kCFNumberSInt32Type);
  assert(array);
  assert(CFArrayGetCount(array) == 2);
  CFRelease(array);

  assert(!cfarray_of_cfnumbers(NULL,
                               sizeof(uint32_t),
                               1,
                               kCFNumberSInt32Type));
  assert(!cfarray_of_cfnumbers(values,
                               sizeof(uint32_t),
                               -1,
                               kCFNumberSInt32Type));
}

static void test_hashtable_fail_closed(void) {
  struct table table = { 0 };
  assert(!table_init(&table, 0, hash_string, compare_string));
  assert(!table_find(&table, "missing"));
  assert(table_init(&table, 2, hash_string, compare_string));
  assert(_table_add(&table, "one", 4, (void*)true));
  assert(_table_add(&table, "two", 4, (void*)true));
  assert(table_find(&table, "one"));
  assert(table_find(&table, "two"));
  struct bucket** buckets = table.buckets;
  int capacity = table.capacity;
  assert(table_clear(&table));
  assert(table.buckets == buckets);
  assert(table.capacity == capacity);
  assert(table.count == 0);
  assert(!table_find(&table, "one"));
  table_free(&table);
}

int main(void) {
  test_argument_codec();
  test_outer_mach_validation();
  test_invalid_mach_message_cleanup();
  test_filter_scope();
  test_cfnumber_arrays();
  test_hashtable_fail_closed();
  puts("safety tests passed");
  return 0;
}
