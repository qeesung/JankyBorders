#include "animation.h"
#include <stdlib.h>
#include <string.h>

struct animation_registration {
  uintptr_t token;
  struct animation* animation;
  struct animation_registration* next;
};

static pthread_mutex_t g_animation_registry_mutex
                                        = PTHREAD_MUTEX_INITIALIZER;
static struct animation_registration* g_animation_registrations;
static uintptr_t g_next_animation_token = 1;

static uintptr_t animation_register(struct animation* animation) {
  struct animation_registration* registration = malloc(sizeof(*registration));
  if (!registration) return 0;

  pthread_mutex_lock(&g_animation_registry_mutex);
  // Zero is reserved for "not registered". Once the 64-bit counter wraps,
  // fail all future registrations rather than reuse a token and permit ABA.
  if (!g_next_animation_token) {
    pthread_mutex_unlock(&g_animation_registry_mutex);
    free(registration);
    return 0;
  }
  uintptr_t token = g_next_animation_token++;
  registration->token = token;
  registration->animation = animation;
  registration->next = g_animation_registrations;
  g_animation_registrations = registration;
  pthread_mutex_unlock(&g_animation_registry_mutex);
  return token;
}

static void animation_unregister(uintptr_t token) {
  if (!token) return;

  pthread_mutex_lock(&g_animation_registry_mutex);
  struct animation_registration** cursor = &g_animation_registrations;
  while (*cursor && (*cursor)->token != token) cursor = &(*cursor)->next;
  struct animation_registration* registration = *cursor;
  if (registration) *cursor = registration->next;
  pthread_mutex_unlock(&g_animation_registry_mutex);
  free(registration);
}

static struct animation* animation_pin(uintptr_t token) {
  if (!token) return NULL;

  pthread_mutex_lock(&g_animation_registry_mutex);
  struct animation_registration* registration = g_animation_registrations;
  while (registration && registration->token != token) {
    registration = registration->next;
  }
  struct animation* animation = registration
                                ? registration->animation
                                : NULL;
  if (animation) {
    atomic_fetch_add_explicit(&animation->active_callbacks,
                              1,
                              memory_order_acq_rel);
  }
  pthread_mutex_unlock(&g_animation_registry_mutex);
  return animation;
}

bool animation_init(struct animation* animation) {
  if (!animation) return false;
  memset(animation, 0, sizeof(struct animation));
  atomic_init(&animation->active_callbacks, 0);
  if (pthread_mutex_init(&animation->mutex, NULL) != 0) return false;
  if (pthread_cond_init(&animation->callbacks_drained, NULL) != 0) {
    pthread_mutex_destroy(&animation->mutex);
    return false;
  }
  return true;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
static CVReturn animation_callback(CVDisplayLinkRef display_link,
                                   const CVTimeStamp* now,
                                   const CVTimeStamp* output_time,
                                   CVOptionFlags flags_in,
                                   CVOptionFlags* flags_out,
                                   void* context) {
  uintptr_t token = (uintptr_t)context;
  // The token registry outlives every border. Unregistration and pinning share
  // one lock, so a callback admitted before stop is counted, while a callback
  // entering arbitrarily late never dereferences released animation storage.
  struct animation* animation = animation_pin(token);
  if (!animation) return kCVReturnSuccess;
  CVReturn result = kCVReturnSuccess;
  pthread_mutex_lock(&animation->mutex);
  if (animation->stopping || !animation->callback || !animation->context) {
    pthread_mutex_unlock(&animation->mutex);
    goto callback_finished;
  }
  CVDisplayLinkOutputCallback callback = animation->callback;
  pthread_mutex_unlock(&animation->mutex);

  result = callback(display_link,
                    now,
                    output_time,
                    flags_in,
                    flags_out,
                    animation);

callback_finished:
  pthread_mutex_lock(&animation->mutex);
  unsigned int previous = atomic_fetch_sub_explicit(
      &animation->active_callbacks,
      1,
      memory_order_acq_rel);
  if (previous == 1) {
    pthread_cond_broadcast(&animation->callbacks_drained);
  }
  pthread_mutex_unlock(&animation->mutex);
  return result;
}

bool animation_start(struct animation* animation,
                     CVDisplayLinkOutputCallback proc,
                     void* context) {
  if (!animation || !proc || !context) {
    free(context);
    return false;
  }

  pthread_mutex_lock(&animation->mutex);
  bool idle = !animation->starting
              && !animation->stopping
              && animation->link == NULL
              && animation->context == NULL;
  if (idle) animation->starting = true;
  pthread_mutex_unlock(&animation->mutex);
  if (!idle) {
    free(context);
    return false;
  }

  CVDisplayLinkRef link = NULL;
  if (CVDisplayLinkCreateWithActiveCGDisplays(&link) != kCVReturnSuccess
      || !link) {
    if (link) CVDisplayLinkRelease(link);
    pthread_mutex_lock(&animation->mutex);
    animation->starting = false;
    pthread_cond_broadcast(&animation->callbacks_drained);
    pthread_mutex_unlock(&animation->mutex);
    free(context);
    return false;
  }

  uintptr_t token = animation_register(animation);
  if (!token) {
    CVDisplayLinkRelease(link);
    pthread_mutex_lock(&animation->mutex);
    animation->starting = false;
    pthread_cond_broadcast(&animation->callbacks_drained);
    pthread_mutex_unlock(&animation->mutex);
    free(context);
    return false;
  }

  CVTime refresh_period = CVDisplayLinkGetNominalOutputVideoRefreshPeriod(link);
  if (refresh_period.timeScale == 0
      || CVDisplayLinkSetOutputCallback(link,
                                        animation_callback,
                                        (void*)token) != kCVReturnSuccess) {
    animation_unregister(token);
    CVDisplayLinkRelease(link);
    pthread_mutex_lock(&animation->mutex);
    animation->starting = false;
    pthread_cond_broadcast(&animation->callbacks_drained);
    pthread_mutex_unlock(&animation->mutex);
    free(context);
    return false;
  }

  if (CVDisplayLinkStart(link) != kCVReturnSuccess) {
    pthread_mutex_lock(&animation->mutex);
    // A failed start is not documented to imply that no callback can run.
    // Publish the link in a disabled state so the regular stop/drain path owns
    // any partially started CoreVideo resource.
    animation->link = link;
    animation->context = context;
    animation->callback = NULL;
    animation->callback_token = token;
    animation->starting = false;
    pthread_cond_broadcast(&animation->callbacks_drained);
    pthread_mutex_unlock(&animation->mutex);
    animation_stop(animation);
    return false;
  }

  pthread_mutex_lock(&animation->mutex);
  animation->link = link;
  animation->context = context;
  animation->callback = proc;
  animation->callback_token = token;
  animation->frame_time = 1e6 * (double)refresh_period.timeValue
                        / (double)refresh_period.timeScale;
  animation->starting = false;
  pthread_cond_broadcast(&animation->callbacks_drained);
  pthread_mutex_unlock(&animation->mutex);
  return true;
}

bool animation_stop(struct animation* animation) {
  if (!animation) return true;

  pthread_mutex_lock(&animation->mutex);
  while (animation->starting || animation->stopping) {
    pthread_cond_wait(&animation->callbacks_drained, &animation->mutex);
  }
  if (!animation->link && !animation->context) {
    pthread_mutex_unlock(&animation->mutex);
    return true;
  }
  animation->stopping = true;
  // Prevent newly admitted frames from entering the user callback while a
  // failed CoreVideo stop is waiting to be retried.
  animation->callback = NULL;
  uintptr_t token = animation->callback_token;
  animation->callback_token = 0;
  animation_unregister(token);
  pthread_cond_broadcast(&animation->callbacks_drained);
  CVDisplayLinkRef link = animation->link;
  pthread_mutex_unlock(&animation->mutex);

  CVReturn stop_result = link ? CVDisplayLinkStop(link) : kCVReturnSuccess;
  bool stopped = true;
  if (link) {
    bool running = CVDisplayLinkIsRunning(link);
    stopped = !running;
    if (stop_result != kCVReturnSuccess && running) stopped = false;
  }

  // CVDisplayLink callbacks run on a separate high-priority thread. Drain any
  // callback that entered before stopping before freeing its context.
  pthread_mutex_lock(&animation->mutex);
  while (atomic_load_explicit(&animation->active_callbacks,
                              memory_order_acquire) > 0) {
    pthread_cond_wait(&animation->callbacks_drained, &animation->mutex);
  }
  if (!stopped) {
    animation->stopping = false;
    pthread_cond_broadcast(&animation->callbacks_drained);
    pthread_mutex_unlock(&animation->mutex);
    return false;
  }
  void* context = animation->context;
  animation->context = NULL;
  animation->link = NULL;
  animation->frame_time = 0.0;
  animation->stopping = false;
  pthread_cond_broadcast(&animation->callbacks_drained);
  pthread_mutex_unlock(&animation->mutex);

  if (link) CVDisplayLinkRelease(link);
  free(context);
  return true;
}

bool animation_destroy(struct animation* animation) {
  if (!animation) return true;
  if (!animation_stop(animation)) return false;
  pthread_cond_destroy(&animation->callbacks_drained);
  pthread_mutex_destroy(&animation->mutex);
  return true;
}
#pragma clang diagnostic pop
