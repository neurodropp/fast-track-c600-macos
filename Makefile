CC := clang
CFLAGS := -std=c11 -Wall -Wextra -Werror -O2
FRAMEWORKS := -framework CoreFoundation -framework IOKit

.PHONY: all clean
all: build/c600-probe build/c600-capture-check

build/c600-probe: src/usb_probe.c
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $< $(FRAMEWORKS)

build/c600-capture-check: src/usb_capture_check.c
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $< $(FRAMEWORKS)

clean:
	rm -rf build
