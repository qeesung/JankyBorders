#pragma once
#include <CoreVideo/CoreVideo.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

struct animation {
  pthread_mutex_t mutex;
  pthread_cond_t callbacks_drained;
  atomic_uint active_callbacks;
  bool starting;
  bool stopping;
  uintptr_t callback_token;
  CVDisplayLinkOutputCallback callback;
  void* context;
  double frame_time;
  CVDisplayLinkRef link;
};

bool animation_init(struct animation* animation);
// Takes ownership of context on both success and failure.
bool animation_start(struct animation* animation,
                     CVDisplayLinkOutputCallback proc,
                     void* context);
// Returns false when CoreVideo still reports a running link. In that case the
// callback is disabled, but the animation storage must remain alive and stop
// should be retried before destruction.
bool animation_stop(struct animation* animation);
// Destruction must not race a new start; border owners enforce this with their
// lifecycle mutex. Concurrent callbacks and stop calls are drained internally.
bool animation_destroy(struct animation* animation);
