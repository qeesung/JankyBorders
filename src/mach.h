#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach.h>
#include <stdbool.h>
#include <stddef.h>

#define BS_NAME "git.felix.borders"
#define MACH_MESSAGE_MAX_PAYLOAD (1024U * 1024U)

struct mach_message {
  mach_msg_header_t header;
  mach_msg_size_t msgh_descriptor_count;
  mach_msg_ool_descriptor_t descriptor;
};

#define MACH_HANDLER(name) void name(void* message, uint32_t len)
typedef MACH_HANDLER(mach_handler);

struct mach_server {
  bool is_running;
  ipc_space_t task;
  mach_port_t port;

  mach_handler* handler;
};

mach_port_t mach_get_bs_port(const char* bs_name);
bool mach_register_port(mach_port_t port, const char* name);
void mach_dispose_port(ipc_space_t task, mach_port_t* port);
bool mach_server_begin(struct mach_server* mach_server, mach_handler handler);
bool mach_send_message(mach_port_t port, void* message, uint32_t len);
bool mach_message_get_payload(void* data,
                              size_t received_size,
                              void** payload,
                              uint32_t* payload_size);
void mach_destroy_received_message(void* data, size_t received_size);
bool mach_encode_arguments(int count,
                           char** arguments,
                           void** payload,
                           uint32_t* payload_size);
bool mach_decode_arguments(void* payload,
                           uint32_t payload_size,
                           char*** arguments,
                           int* count);
