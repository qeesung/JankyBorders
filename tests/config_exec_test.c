#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/misc/helpers.h"

static bool wait_for_file(const char* path) {
  for (int attempt = 0; attempt < 100; ++attempt) {
    if (access(path, F_OK) == 0) return true;
    usleep(10000);
  }
  return false;
}

int main(void) {
  char config_home[] = "/tmp/jankyborders config;literal.XXXXXX";
  assert(mkdtemp(config_home));

  char config_directory[PATH_MAX];
  char config_path[PATH_MAX];
  char marker_path[PATH_MAX];
  assert(snprintf(config_directory,
                  sizeof(config_directory),
                  "%s/borders",
                  config_home) < (int)sizeof(config_directory));
  assert(mkdir(config_directory, 0700) == 0);
  assert(snprintf(config_path,
                  sizeof(config_path),
                  "%s/bordersrc",
                  config_directory) < (int)sizeof(config_path));
  assert(snprintf(marker_path,
                  sizeof(marker_path),
                  "%s/executed",
                  config_home) < (int)sizeof(marker_path));

  FILE* config = fopen(config_path, "w");
  assert(config);
  assert(fputs("#!/bin/bash\n"
               "values=(safe execution)\n"
               "printf '%s' \"$0\" > \"$BORDERS_TEST_MARKER\"\n",
               config) >= 0);
  assert(fclose(config) == 0);

  assert(setenv("XDG_CONFIG_HOME", config_home, 1) == 0);
  assert(setenv("BORDERS_TEST_MARKER", marker_path, 1) == 0);
  execute_config_file("borders", "bordersrc");
  assert(wait_for_file(marker_path));

  FILE* marker = fopen(marker_path, "r");
  assert(marker);
  char executed_path[PATH_MAX] = { 0 };
  assert(fgets(executed_path, sizeof(executed_path), marker));
  assert(fclose(marker) == 0);
  assert(strcmp(executed_path, config_path) == 0);

  assert(unlink(marker_path) == 0);
  config = fopen(config_path, "w");
  assert(config);
  assert(fputs("printf '%s' \"$0\" > \"$BORDERS_TEST_MARKER\"\n",
               config) >= 0);
  assert(fclose(config) == 0);
  execute_config_file("borders", "bordersrc");
  assert(wait_for_file(marker_path));

  marker = fopen(marker_path, "r");
  assert(marker);
  memset(executed_path, 0, sizeof(executed_path));
  assert(fgets(executed_path, sizeof(executed_path), marker));
  assert(fclose(marker) == 0);
  assert(strcmp(executed_path, config_path) == 0);

  assert(unlink(marker_path) == 0);
  assert(unlink(config_path) == 0);
  assert(rmdir(config_directory) == 0);
  assert(rmdir(config_home) == 0);
  puts("config execution preserves literal paths and legacy scripts: ok");
  return 0;
}
