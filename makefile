FILES = src/main.c src/parse.c src/mach.c src/hashtable.c src/events.c src/windows.c src/border.c src/animation.c 
LIBS = -framework AppKit -framework CoreVideo -F/System/Library/PrivateFrameworks/ -framework SkyLight
SAFETY_TEST_FILES = tests/safety_tests.c src/parse.c src/mach.c src/hashtable.c

all: | bin
	clang -std=c99 -O3 -g $(FILES) -o bin/borders $(LIBS)

debug: | bin
	clang -std=c99 -O0 -g -DDEBUG $(FILES) -o bin/debug $(LIBS)

asan: | bin
	clang -std=c99 -Wall -g -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -g $(FILES) -o bin/debug $(LIBS)
	./bin/debug

test: | bin
	clang -std=c99 -Wall -Wextra -O0 -g -Isrc $(SAFETY_TEST_FILES) -o bin/safety_tests $(LIBS)
	./bin/safety_tests
	clang -std=c99 -Wall -Wextra -O0 -g -ffunction-sections tests/color_style_test.c src/hashtable.c -Wl,-dead_strip $(LIBS) -o bin/color_style_test
	./bin/color_style_test

test-sanitize: | bin
	clang -std=c99 -Wall -Wextra -O1 -g -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -Isrc $(SAFETY_TEST_FILES) -o bin/safety_tests_sanitize $(LIBS)
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ./bin/safety_tests_sanitize
bin:
	mkdir bin

clean:
	rm -rf bin
