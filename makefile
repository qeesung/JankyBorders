FILES = src/main.c src/parse.c src/mach.c src/hashtable.c src/events.c src/windows.c src/border.c src/animation.c src/screen_capture.c src/space_bridge.m
HEADERS = $(wildcard src/*.h src/misc/*.h)
LIBS = -framework AppKit -framework CoreVideo -F/System/Library/PrivateFrameworks/ -framework SkyLight
SAFETY_TEST_FILES = tests/safety_tests.c src/parse.c src/mach.c src/hashtable.c
SANITIZER_FLAGS = -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer
THREAD_SANITIZER_FLAGS = -fsanitize=thread -fno-omit-frame-pointer
DEPLOYMENT_FLAGS = -mmacosx-version-min=14.0

all: bin/borders

bin/borders: $(FILES) $(HEADERS) makefile | bin
	clang -std=c99 $(DEPLOYMENT_FLAGS) -O3 -g $(FILES) -o bin/borders $(LIBS)

debug: | bin
	clang -std=c99 $(DEPLOYMENT_FLAGS) -O0 -g -DDEBUG $(FILES) -o bin/debug $(LIBS)

warnings: | bin
	clang -std=c99 $(DEPLOYMENT_FLAGS) -Wall -Wextra -Werror -O0 -g $(FILES) -o bin/warnings-check $(LIBS)
	/bin/bash -n scripts/local-service

asan: | bin
	clang -std=c99 $(DEPLOYMENT_FLAGS) -Wall -g -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -g $(FILES) -o bin/debug $(LIBS)
	./bin/debug

test: | bin
	clang -std=c99 -Wall -Wextra -O0 -g -Isrc $(SAFETY_TEST_FILES) -o bin/safety_tests $(LIBS)
	./bin/safety_tests
	clang -std=c99 -Wall -Wextra -O0 -g -ffunction-sections tests/color_style_test.c src/hashtable.c -Wl,-dead_strip $(LIBS) -o bin/color_style_test
	./bin/color_style_test
	clang -std=c99 -Wall -Wextra -O0 -g tests/config_path_test.c -o bin/config_path_test
	./bin/config_path_test
	clang -std=c99 -Wall -Wextra -O0 -g tests/config_exec_test.c -o bin/config_exec_test $(LIBS)
	./bin/config_exec_test
	clang -std=c99 -Wall -Wextra -Werror -O0 -g tests/test_active_only.c -o bin/test_active_only
	./bin/test_active_only
	clang -std=c99 -Wall -Wextra -Werror -O0 -g tests/window_policy_test.c -o bin/window_policy_test
	./bin/window_policy_test
	clang -std=c99 -Wall -Wextra -Werror -O0 -g tests/geometry_test.c -o bin/geometry_test -framework CoreGraphics
	./bin/geometry_test
	clang -std=c99 -Wall -Wextra -Werror -O0 -g tests/space_recovery_test.c -o bin/space_recovery_test
	./bin/space_recovery_test
	clang -std=c99 -Wall -Wextra -Werror -O0 -g tests/animation_lifecycle_test.c -o bin/animation_lifecycle_test -framework CoreVideo
	./bin/animation_lifecycle_test

test-sanitize: | bin
	clang -std=c99 -Wall -Wextra -O1 -g $(SANITIZER_FLAGS) -Isrc $(SAFETY_TEST_FILES) -o bin/safety_tests_sanitize $(LIBS)
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ./bin/safety_tests_sanitize
	clang -std=c99 -Wall -Wextra -O1 -g $(SANITIZER_FLAGS) tests/color_style_test.c src/hashtable.c -o bin/color_style_test_sanitize $(LIBS)
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ./bin/color_style_test_sanitize
	clang -std=c99 -Wall -Wextra -O1 -g $(SANITIZER_FLAGS) tests/config_path_test.c -o bin/config_path_test_sanitize
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ./bin/config_path_test_sanitize
	clang -std=c99 -Wall -Wextra -O1 -g $(SANITIZER_FLAGS) tests/config_exec_test.c -o bin/config_exec_test_sanitize $(LIBS)
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ./bin/config_exec_test_sanitize
	clang -std=c99 -Wall -Wextra -Werror -O1 -g $(SANITIZER_FLAGS) tests/test_active_only.c -o bin/test_active_only_sanitize
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ./bin/test_active_only_sanitize
	clang -std=c99 -Wall -Wextra -Werror -O1 -g $(SANITIZER_FLAGS) tests/window_policy_test.c -o bin/window_policy_test_sanitize
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ./bin/window_policy_test_sanitize
	clang -std=c99 -Wall -Wextra -Werror -O1 -g $(SANITIZER_FLAGS) tests/geometry_test.c -o bin/geometry_test_sanitize -framework CoreGraphics
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ./bin/geometry_test_sanitize
	clang -std=c99 -Wall -Wextra -Werror -O1 -g $(SANITIZER_FLAGS) tests/space_recovery_test.c -o bin/space_recovery_test_sanitize
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ./bin/space_recovery_test_sanitize
	clang -std=c99 -Wall -Wextra -Werror -O1 -g $(SANITIZER_FLAGS) tests/animation_lifecycle_test.c -o bin/animation_lifecycle_test_sanitize -framework CoreVideo
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ./bin/animation_lifecycle_test_sanitize

test-thread: | bin
	clang -std=c99 -Wall -Wextra -Werror -O1 -g $(THREAD_SANITIZER_FLAGS) tests/animation_lifecycle_test.c -o bin/animation_lifecycle_test_tsan -framework CoreVideo
	TSAN_OPTIONS=halt_on_error=1 ./bin/animation_lifecycle_test_tsan

install-service update-service:
	@/bin/bash scripts/local-service install

service-start:
	@/bin/bash scripts/local-service start

service-stop:
	@/bin/bash scripts/local-service stop

service-restart:
	@/bin/bash scripts/local-service restart

rollback-service:
	@/bin/bash scripts/local-service rollback

service-status:
	@/bin/bash scripts/local-service status

signing-help:
	@/bin/bash scripts/local-service signing-help

configure-signing:
	@JANKYBORDERS_SIGNING_IDENTITY="$(JANKYBORDERS_SIGNING_IDENTITY)" /bin/bash scripts/local-service configure-signing

signing-status:
	@/bin/bash scripts/local-service signing-status

screen-capture-status:
	@/bin/bash scripts/local-service screen-capture-status

request-screen-capture:
	@/bin/bash scripts/local-service request-screen-capture

uninstall-service:
	@/bin/bash scripts/local-service uninstall

bin:
	mkdir bin

clean:
	rm -rf bin

.PHONY: all debug warnings asan test test-sanitize test-thread clean install-service update-service service-start service-stop service-restart rollback-service service-status signing-help configure-signing signing-status screen-capture-status request-screen-capture uninstall-service
