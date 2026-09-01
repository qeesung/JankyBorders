#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef bool (*config_file_exists_fn)(const char* path);

static inline bool config_candidate(char* path,
                                    size_t path_size,
                                    const char* base,
                                    const char* suffix,
                                    config_file_exists_fn exists) {
  if (!base || base[0] != '/' || !*suffix) return false;

  size_t base_length = strlen(base);
  while (base_length > 1 && base[base_length - 1] == '/') base_length--;
  int length = snprintf(path,
                        path_size,
                        "%.*s/%s",
                        (int)base_length,
                        base,
                        suffix          );
  return length >= 0
         && (size_t)length < path_size
         && exists(path);
}

static inline bool config_resolve_path(char* path,
                                       size_t path_size,
                                       const char* name,
                                       const char* filename,
                                       const char* xdg_config_home,
                                       const char* home,
                                       config_file_exists_fn exists) {
  if (!path || !path_size || !name || !*name || !filename || !*filename
      || !exists) {
    return false;
  }

  char suffix[path_size];
  int length = snprintf(suffix, path_size, "%s/%s", name, filename);
  if (length < 0 || (size_t)length >= path_size) return false;

  if (config_candidate(path,
                       path_size,
                       xdg_config_home,
                       suffix,
                       exists           )) {
    return true;
  }

  length = snprintf(suffix, path_size, ".config/%s/%s", name, filename);
  if (length >= 0
      && (size_t)length < path_size
      && config_candidate(path, path_size, home, suffix, exists)) {
    return true;
  }

  length = snprintf(suffix, path_size, ".%s", filename);
  return length >= 0
         && (size_t)length < path_size
         && config_candidate(path, path_size, home, suffix, exists);
}
