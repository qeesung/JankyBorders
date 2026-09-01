#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdlib.h>
#include "../src/animation.c"

struct callback_gate {
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  bool entered;
  bool release;
};

struct stop_probe {
  struct animation* animation;
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  bool started;
  bool returned;
  bool stopped;
};

static CVReturn blocking_callback(CVDisplayLinkRef display_link,
                                  const CVTimeStamp* now,
                                  const CVTimeStamp* output_time,
                                  CVOptionFlags flags_in,
                                  CVOptionFlags* flags_out,
                                  void* context) {
  (void)display_link;
  (void)now;
  (void)output_time;
  (void)flags_in;
  (void)flags_out;
  struct animation* animation = context;
  struct callback_gate* gate = animation->context;

  pthread_mutex_lock(&gate->mutex);
  gate->entered = true;
  pthread_cond_broadcast(&gate->condition);
  while (!gate->release) {
    pthread_cond_wait(&gate->condition, &gate->mutex);
  }
  pthread_mutex_unlock(&gate->mutex);
  pthread_cond_destroy(&gate->condition);
  pthread_mutex_destroy(&gate->mutex);
  return kCVReturnSuccess;
}

static CVReturn unreachable_callback(CVDisplayLinkRef display_link,
                                     const CVTimeStamp* now,
                                     const CVTimeStamp* output_time,
                                     CVOptionFlags flags_in,
                                     CVOptionFlags* flags_out,
                                     void* context) {
  (void)display_link;
  (void)now;
  (void)output_time;
  (void)flags_in;
  (void)flags_out;
  (void)context;
  assert(false && "an unregistered callback token must not be dispatched");
  return kCVReturnSuccess;
}

static void* run_callback(void* context) {
  CVTimeStamp timestamp = { 0 };
  CVOptionFlags flags_out = 0;
  animation_callback(NULL,
                     &timestamp,
                     &timestamp,
                     0,
                     &flags_out,
                     context);
  return NULL;
}

static uintptr_t prepare_manual_animation(
    struct animation* animation,
    CVDisplayLinkOutputCallback callback,
    void* context) {
  uintptr_t token = animation_register(animation);
  assert(token);
  pthread_mutex_lock(&animation->mutex);
  animation->callback = callback;
  animation->context = context;
  animation->callback_token = token;
  pthread_mutex_unlock(&animation->mutex);
  return token;
}

static void* stop_animation(void* context) {
  struct stop_probe* probe = context;
  pthread_mutex_lock(&probe->mutex);
  probe->started = true;
  pthread_cond_broadcast(&probe->condition);
  pthread_mutex_unlock(&probe->mutex);
  bool stopped = animation_stop(probe->animation);
  pthread_mutex_lock(&probe->mutex);
  probe->stopped = stopped;
  probe->returned = true;
  pthread_cond_broadcast(&probe->condition);
  pthread_mutex_unlock(&probe->mutex);
  return NULL;
}

static void stop_probe_init(struct stop_probe* probe,
                            struct animation* animation) {
  *probe = (struct stop_probe) {
    .animation = animation,
  };
  assert(pthread_mutex_init(&probe->mutex, NULL) == 0);
  assert(pthread_cond_init(&probe->condition, NULL) == 0);
}

static void stop_probe_wait_until_started(struct stop_probe* probe) {
  pthread_mutex_lock(&probe->mutex);
  while (!probe->started) {
    pthread_cond_wait(&probe->condition, &probe->mutex);
  }
  pthread_mutex_unlock(&probe->mutex);
}

static void stop_probe_destroy(struct stop_probe* probe) {
  pthread_cond_destroy(&probe->condition);
  pthread_mutex_destroy(&probe->mutex);
}

static void test_stop_waits_for_start_to_settle(void) {
  struct animation animation;
  assert(animation_init(&animation));

  pthread_mutex_lock(&animation.mutex);
  animation.starting = true;
  pthread_mutex_unlock(&animation.mutex);

  struct stop_probe probe;
  stop_probe_init(&probe, &animation);
  pthread_t stop_thread;
  assert(pthread_create(&stop_thread, NULL, stop_animation, &probe) == 0);
  stop_probe_wait_until_started(&probe);

  pthread_mutex_lock(&probe.mutex);
  assert(!probe.returned);
  pthread_mutex_unlock(&probe.mutex);

  pthread_mutex_lock(&animation.mutex);
  animation.starting = false;
  pthread_cond_broadcast(&animation.callbacks_drained);
  pthread_mutex_unlock(&animation.mutex);

  assert(pthread_join(stop_thread, NULL) == 0);
  pthread_mutex_lock(&probe.mutex);
  assert(probe.returned);
  assert(probe.stopped);
  pthread_mutex_unlock(&probe.mutex);
  stop_probe_destroy(&probe);
  assert(animation_destroy(&animation));
}

static void test_callback_counts_before_lifecycle_lock(void) {
  struct animation animation;
  assert(animation_init(&animation));

  struct callback_gate* gate = calloc(1, sizeof(*gate));
  assert(gate);
  assert(pthread_mutex_init(&gate->mutex, NULL) == 0);
  assert(pthread_cond_init(&gate->condition, NULL) == 0);
  gate->release = true;

  uintptr_t token = prepare_manual_animation(&animation,
                                              blocking_callback,
                                              gate);
  pthread_mutex_lock(&animation.mutex);
  pthread_t callback_thread;
  assert(pthread_create(&callback_thread,
                        NULL,
                        run_callback,
                        (void*)token) == 0);

  unsigned int entrants = 0;
  for (int attempt = 0; attempt < 100000 && entrants == 0; ++attempt) {
    entrants = atomic_load_explicit(&animation.active_callbacks,
                                    memory_order_acquire);
    if (entrants == 0) sched_yield();
  }
  assert(entrants == 1);
  pthread_mutex_unlock(&animation.mutex);

  assert(pthread_join(callback_thread, NULL) == 0);
  assert(animation_destroy(&animation));
}

static void test_late_callback_cannot_touch_destroyed_animation(void) {
  struct animation* animation = malloc(sizeof(*animation));
  assert(animation);
  assert(animation_init(animation));
  void* payload = malloc(16);
  assert(payload);
  uintptr_t old_token = prepare_manual_animation(animation,
                                                  unreachable_callback,
                                                  payload);
  assert(animation_destroy(animation));
  free(animation);

  CVTimeStamp timestamp = { 0 };
  CVOptionFlags flags_out = 0;
  assert(animation_callback(NULL,
                            &timestamp,
                            &timestamp,
                            0,
                            &flags_out,
                            (void*)old_token) == kCVReturnSuccess);
}

static void test_stop_drains_an_inflight_callback(void) {
  struct animation animation;
  assert(animation_init(&animation));

  struct callback_gate* gate = calloc(1, sizeof(*gate));
  assert(gate);
  assert(pthread_mutex_init(&gate->mutex, NULL) == 0);
  assert(pthread_cond_init(&gate->condition, NULL) == 0);

  uintptr_t token = prepare_manual_animation(&animation,
                                              blocking_callback,
                                              gate);

  pthread_t callback_thread;
  assert(pthread_create(&callback_thread,
                        NULL,
                        run_callback,
                        (void*)token) == 0);
  pthread_mutex_lock(&gate->mutex);
  while (!gate->entered) {
    pthread_cond_wait(&gate->condition, &gate->mutex);
  }
  pthread_mutex_unlock(&gate->mutex);

  struct stop_probe probe;
  stop_probe_init(&probe, &animation);
  pthread_t stop_thread;
  assert(pthread_create(&stop_thread, NULL, stop_animation, &probe) == 0);
  stop_probe_wait_until_started(&probe);

  pthread_mutex_lock(&animation.mutex);
  while (!animation.stopping) {
    pthread_cond_wait(&animation.callbacks_drained, &animation.mutex);
  }
  pthread_mutex_unlock(&animation.mutex);
  pthread_mutex_lock(&probe.mutex);
  assert(!probe.returned);
  pthread_mutex_unlock(&probe.mutex);

  pthread_mutex_lock(&gate->mutex);
  gate->release = true;
  pthread_cond_broadcast(&gate->condition);
  pthread_mutex_unlock(&gate->mutex);

  assert(pthread_join(callback_thread, NULL) == 0);
  assert(pthread_join(stop_thread, NULL) == 0);
  pthread_mutex_lock(&probe.mutex);
  assert(probe.returned);
  assert(probe.stopped);
  pthread_mutex_unlock(&probe.mutex);
  stop_probe_destroy(&probe);
  assert(animation_destroy(&animation));
}

int main(void) {
  for (int i = 0; i < 1000; ++i) {
    struct animation animation;
    assert(animation_init(&animation));
    assert(animation_stop(&animation));

    void* context = malloc(16);
    assert(context);
    assert(!animation_start(&animation, NULL, context));
    assert(animation_destroy(&animation));
  }
  test_stop_waits_for_start_to_settle();
  test_callback_counts_before_lifecycle_lock();
  test_late_callback_cannot_touch_destroyed_animation();
  test_stop_drains_an_inflight_callback();
  pthread_mutex_lock(&g_animation_registry_mutex);
  assert(g_animation_registrations == NULL);
  pthread_mutex_unlock(&g_animation_registry_mutex);
  return 0;
}
