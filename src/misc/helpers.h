#pragma once
#include "extern.h"
#include "config.h"
#include "sys/stat.h"
#include <limits.h>
#include "ApplicationServices/ApplicationServices.h"
#include <stdint.h>
#include <stdlib.h>

#define DELAY_ASYNC_EXEC_ON_MAIN_THREAD(delay, code) {\
  dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_LOW, 0), ^{\
    usleep(delay);\
    dispatch_async(dispatch_get_main_queue(), ^{\
      code\
    });\
  });\
}

static inline void debug(const char* message, ...) {
#ifdef DEBUG
  va_list va;
  va_start(va, message);
  vprintf(message, va);
  va_end(va);
#else
  (void)message;
#endif
}

static inline void error(const char* message, ...) {
  va_list va;
  va_start(va, message);
  vfprintf(stderr, message, va);
  va_end(va);
  exit(EXIT_FAILURE);
}

static inline bool file_exists(const char* filename) {
  struct stat buffer;
  if (stat(filename, &buffer) != 0) return false;
  if (buffer.st_mode & S_IFDIR) return false;
  return true;
}

static inline bool file_setx(const char* filename) {
  struct stat buffer;
  if (stat(filename, &buffer) != 0) return false;
  bool is_executable = buffer.st_mode & S_IXUSR;
  if (!is_executable && chmod(filename, S_IXUSR | buffer.st_mode) != 0) {
    return false;
  }
  return true;
}

static inline void execute_config_file(const char* name, const char* filename) {
  char path[PATH_MAX];
  if (!config_resolve_path(path,
                           sizeof(path),
                           name,
                           filename,
                           getenv("XDG_CONFIG_HOME"),
                           getenv("HOME"),
                           file_exists                )) {
    debug("No config file found...\n");
    return;
  }

  if (!file_setx(path)) {
    printf("[!] Failed to make config at '%s' executable...\n", path);
    return;
  }

  signal(SIGCHLD, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);

  int pid = fork();
  if (pid !=  0) return;

  alarm(60);
  char* exec[] = { "/usr/bin/env", "sh", "-c", path, NULL };
  exit(execvp(exec[0], exec));
}

static inline CFArrayRef cfarray_of_cfnumbers(const void* values,
                                              size_t size,
                                              int count,
                                              CFNumberType type) {
  if (count < 0) return NULL;
  if (count == 0) {
    return CFArrayCreate(NULL, NULL, 0, &kCFTypeArrayCallBacks);
  }
  if (!values || size == 0
      || (size_t)count > SIZE_MAX / sizeof(CFNumberRef)
      || (size_t)count > SIZE_MAX / size) {
    return NULL;
  }

  CFNumberRef* temp = calloc((size_t)count, sizeof(CFNumberRef));
  if (!temp) return NULL;
  for (int i = 0; i < count; ++i) {
    temp[i] = CFNumberCreate(NULL,
                             type,
                             ((const char*)values) + (size * (size_t)i));
    if (!temp[i]) {
      for (int j = 0; j < i; ++j) CFRelease(temp[j]);
      free(temp);
      return NULL;
    }
  }

  CFArrayRef result = CFArrayCreate(NULL,
                                    (const void **)temp,
                                    count,
                                    &kCFTypeArrayCallBacks);

  for (int i = 0; i < count; ++i) CFRelease(temp[i]);
  free(temp);

  return result;
}
