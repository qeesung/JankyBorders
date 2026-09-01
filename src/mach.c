#include <bootstrap.h>
#include <CoreFoundation/CoreFoundation.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "mach.h"

mach_port_t mach_get_bs_port(char* bs_name) {
  mach_port_name_t task = mach_task_self();

  mach_port_t bs_port = 0;
  if (task_get_special_port(task,
                            TASK_BOOTSTRAP_PORT,
                            &bs_port            ) != KERN_SUCCESS) {
    return 0;
  }

  mach_port_t port = 0;
  if (bootstrap_look_up(bs_port,
                        bs_name,
                        &port   ) != KERN_SUCCESS) {
    return 0;
  }

  return port;
}

bool mach_send_message(mach_port_t port, void* message, uint32_t len) {
  if (!message || !port || len == 0 || len > MACH_MESSAGE_MAX_PAYLOAD) {
    return false;
  }

  struct mach_message msg = { 0 };
  msg.header.msgh_remote_port = port;
  msg.header.msgh_bits = MACH_MSGH_BITS_SET(MACH_MSG_TYPE_COPY_SEND
                                            & MACH_MSGH_BITS_REMOTE_MASK,
                                            0,
                                            0,
                                            MACH_MSGH_BITS_COMPLEX       );

  msg.header.msgh_size = sizeof(struct mach_message);

  msg.msgh_descriptor_count = 1;
  msg.descriptor.address = message;
  msg.descriptor.size = len;
  msg.descriptor.copy = MACH_MSG_VIRTUAL_COPY;
  msg.descriptor.deallocate = false;
  msg.descriptor.type = MACH_MSG_OOL_DESCRIPTOR;

  return mach_msg(&msg.header,
                  MACH_SEND_MSG,
                  sizeof(struct mach_message),
                  0,
                  MACH_PORT_NULL,
                  MACH_MSG_TIMEOUT_NONE,
                  MACH_PORT_NULL             ) == KERN_SUCCESS;
}

bool mach_message_get_payload(void* data,
                              size_t received_size,
                              void** payload,
                              uint32_t* payload_size) {
  if (!payload || !payload_size) return false;
  *payload = NULL;
  *payload_size = 0;
  if (!data || received_size < sizeof(struct mach_message)) {
    return false;
  }

  struct mach_message* message = data;
  if (message->header.msgh_size != sizeof(struct mach_message)
      || message->header.msgh_size > received_size
      || !(message->header.msgh_bits & MACH_MSGH_BITS_COMPLEX)
      || message->msgh_descriptor_count != 1
      || message->descriptor.type != MACH_MSG_OOL_DESCRIPTOR
      || (message->descriptor.size > 0 && !message->descriptor.address)) {
    return false;
  }

  *payload = message->descriptor.address;
  *payload_size = message->descriptor.size;
  return true;
}

void mach_destroy_received_message(void* data, size_t received_size) {
  if (!data || received_size < sizeof(mach_msg_header_t)) return;

  mach_msg_header_t* header = data;
  if (header->msgh_size < sizeof(mach_msg_header_t)
      || header->msgh_size > received_size) {
    return;
  }
  mach_msg_destroy(header);
}

bool mach_encode_arguments(int count,
                           char** arguments,
                           void** payload,
                           uint32_t* payload_size) {
  if (!payload || !payload_size) return false;
  *payload = NULL;
  *payload_size = 0;
  if (count <= 0 || !arguments) return false;

  size_t size = 1;
  for (int i = 0; i < count; ++i) {
    if (!arguments[i] || arguments[i][0] == '\0') return false;
    size_t argument_length = strlen(arguments[i]);
    if (argument_length == SIZE_MAX) return false;
    size_t argument_size = argument_length + 1;
    if (argument_size > MACH_MESSAGE_MAX_PAYLOAD - size) return false;
    size += argument_size;
  }

  char* encoded = calloc(size, 1);
  if (!encoded) return false;

  char* cursor = encoded;
  for (int i = 0; i < count; ++i) {
    size_t argument_size = strlen(arguments[i]) + 1;
    memcpy(cursor, arguments[i], argument_size);
    cursor += argument_size;
  }

  *payload = encoded;
  *payload_size = (uint32_t)size;
  return true;
}

bool mach_decode_arguments(void* payload,
                           uint32_t payload_size,
                           char*** arguments,
                           int* count) {
  if (!arguments || !count) return false;
  *arguments = NULL;
  *count = 0;
  if (!payload || payload_size < 3
      || payload_size > MACH_MESSAGE_MAX_PAYLOAD) {
    return false;
  }

  char* cursor = payload;
  char* end = cursor + payload_size;
  size_t argument_count = 0;
  bool found_end = false;

  while (cursor < end) {
    char* terminator = memchr(cursor, '\0', (size_t)(end - cursor));
    if (!terminator) return false;
    if (terminator == cursor) {
      size_t trailing_size = (size_t)(end - (terminator + 1));
      // Releases before the bounded codec over-allocated one trailing byte per
      // argument. Accept exactly that legacy shape, but no arbitrary suffix.
      found_end = argument_count > 0
                  && (trailing_size == 0
                      || trailing_size == argument_count);
      break;
    }
    if (argument_count == INT_MAX) return false;
    ++argument_count;
    cursor = terminator + 1;
  }

  if (!found_end) return false;
  char** decoded = calloc(argument_count, sizeof(char*));
  if (!decoded) return false;

  cursor = payload;
  for (size_t i = 0; i < argument_count; ++i) {
    decoded[i] = cursor;
    cursor += strlen(cursor) + 1;
  }

  *arguments = decoded;
  *count = (int)argument_count;
  return true;
}

void mach_message_callback(CFMachPortRef port, void* data, CFIndex size, void* context) {
  (void)port;
  if (size < 0) return;

  void* payload = NULL;
  uint32_t payload_size = 0;
  if (!mach_message_get_payload(data,
                                (size_t)size,
                                &payload,
                                &payload_size)) {
    mach_destroy_received_message(data, (size_t)size);
    return;
  }

  struct mach_server* mach_server = context;
  if (mach_server && mach_server->handler) {
    mach_server->handler(payload, payload_size);
  }
  mach_destroy_received_message(data, (size_t)size);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
bool mach_register_port(mach_port_t port, char* name) {
  mach_port_name_t task = mach_task_self();
  mach_port_t bs_port = 0;

  if (task_get_special_port(task,
                            TASK_BOOTSTRAP_PORT,
                            &bs_port) != KERN_SUCCESS) {
    return false;
  }

  if (bootstrap_register(bs_port, name, port) != KERN_SUCCESS) {
    return false;
  }

  return true;
}


bool mach_server_begin(struct mach_server* mach_server, mach_handler handler) {
  if (!mach_server || !handler) return false;
  mach_server->task = mach_task_self();

  if (mach_port_allocate(mach_server->task,
                         MACH_PORT_RIGHT_RECEIVE,
                         &mach_server->port      ) != KERN_SUCCESS) {
    return false;
  }

  struct mach_port_limits limits = {};
  limits.mpl_qlimit = MACH_PORT_QLIMIT_LARGE;

  if (mach_port_set_attributes(mach_server->task,
                               mach_server->port,
                               MACH_PORT_LIMITS_INFO,
                               (mach_port_info_t)&limits,
                               MACH_PORT_LIMITS_INFO_COUNT) != KERN_SUCCESS) {
    return false;
  }

  if (mach_port_insert_right(mach_server->task,
                             mach_server->port,
                             mach_server->port,
                             MACH_MSG_TYPE_MAKE_SEND) != KERN_SUCCESS) {
    return false;
  }

  if (!mach_register_port(mach_server->port, BS_NAME)) return false;

  mach_server->handler = handler;

  CFMachPortContext context = {
    .version = 0,
    .info = (void*)mach_server,
    .retain = NULL,
    .release = NULL,
    .copyDescription = NULL
  };

  CFMachPortRef cf_mach_port = CFMachPortCreateWithPort(NULL,
                                                        mach_server->port,
                                                        mach_message_callback,
                                                        &context,
                                                        false                );
  if (!cf_mach_port) return false;

  CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(NULL,
                                                            cf_mach_port,
                                                            0            );
  if (!source) {
    CFRelease(cf_mach_port);
    return false;
  }

  CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopDefaultMode);
  mach_server->is_running = true;
  CFRelease(source);
  CFRelease(cf_mach_port);
  return true;
}
#pragma clang diagnostic pop
