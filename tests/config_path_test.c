#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/misc/config.h"

static const char* existing_paths[4];

static bool mock_exists(const char* path) {
  for (size_t i = 0; i < sizeof(existing_paths) / sizeof(existing_paths[0]); i++) {
    if (existing_paths[i] && strcmp(path, existing_paths[i]) == 0) return true;
  }
  return false;
}

static void reset_paths(void) {
  memset(existing_paths, 0, sizeof(existing_paths));
}

int main(void) {
  char path[256];

  reset_paths();
  existing_paths[0] = "/tmp/xdg/borders/bordersrc";
  existing_paths[1] = "/Users/test/.config/borders/bordersrc";
  assert(config_resolve_path(path,
                             sizeof(path),
                             "borders",
                             "bordersrc",
                             "/tmp/xdg/",
                             "/Users/test",
                             mock_exists));
  assert(strcmp(path, "/tmp/xdg/borders/bordersrc") == 0);

  reset_paths();
  existing_paths[0] = "/Users/test/.config/borders/bordersrc";
  assert(config_resolve_path(path,
                             sizeof(path),
                             "borders",
                             "bordersrc",
                             "/tmp/missing",
                             "/Users/test",
                             mock_exists));
  assert(strcmp(path, "/Users/test/.config/borders/bordersrc") == 0);

  reset_paths();
  existing_paths[0] = "/Users/test/.config/borders/bordersrc";
  existing_paths[1] = "relative/borders/bordersrc";
  assert(config_resolve_path(path,
                             sizeof(path),
                             "borders",
                             "bordersrc",
                             "relative",
                             "/Users/test",
                             mock_exists));
  assert(strcmp(path, "/Users/test/.config/borders/bordersrc") == 0);

  reset_paths();
  existing_paths[0] = "/Users/test/.bordersrc";
  assert(config_resolve_path(path,
                             sizeof(path),
                             "borders",
                             "bordersrc",
                             NULL,
                             "/Users/test",
                             mock_exists));
  assert(strcmp(path, "/Users/test/.bordersrc") == 0);

  reset_paths();
  existing_paths[0] = "/tmp/xdg/borders/bordersrc";
  assert(config_resolve_path(path,
                             sizeof(path),
                             "borders",
                             "bordersrc",
                             "/tmp/xdg",
                             NULL,
                             mock_exists));
  assert(strcmp(path, "/tmp/xdg/borders/bordersrc") == 0);

  reset_paths();
  assert(!config_resolve_path(path,
                              sizeof(path),
                              "borders",
                              "bordersrc",
                              NULL,
                              NULL,
                              mock_exists));

  char small_path[8];
  existing_paths[0] = "/x/b/b";
  assert(!config_resolve_path(small_path,
                              sizeof(small_path),
                              "borders",
                              "bordersrc",
                              "/x",
                              NULL,
                              mock_exists));

  puts("config path precedence: ok");
  return 0;
}
