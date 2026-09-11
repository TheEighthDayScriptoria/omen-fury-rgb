CC ?= cc
CFLAGS ?= -O2 -g
CPPFLAGS += -Iinclude/uapi -Isrc
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror
KDIR ?= /lib/modules/$(shell uname -r)/build
PREFIX ?= /usr/local
DESTDIR ?=

CLI_OBJECTS := build/cli.o build/transport.o build/protocol.o

.PHONY: all userspace module test install install-userspace install-module clean

all: userspace module

userspace: build/omen-furyctl

build/omen-furyctl: $(CLI_OBJECTS)
	$(CC) $(CFLAGS) $^ -o $@

build/%.o: src/%.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -D_POSIX_C_SOURCE=200809L -c $< -o $@

build:
	mkdir -p $@

module:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/kernel modules

build/test_protocol: tests/test_protocol.c src/protocol.c src/protocol.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -D_POSIX_C_SOURCE=200809L \
		tests/test_protocol.c src/protocol.c -o $@

test: build/test_protocol build/omen-furyctl
	./build/test_protocol
	./build/omen-furyctl --dry-run info
	./build/omen-furyctl --dry-run --dimm C2 static 123456 --brightness 32
	./build/omen-furyctl --dry-run off
	./build/omen-furyctl --dry-run --experimental raw C0 31 FF
	@if ./build/omen-furyctl --dry-run --experimental raw C0 34 FF; then \
		echo "raw whitelist rejection test failed" >&2; exit 1; \
	else \
		echo "raw whitelist rejection: PASS"; \
	fi

install: install-userspace install-module

install-userspace: build/omen-furyctl
	install -D -m 0755 build/omen-furyctl $(DESTDIR)$(PREFIX)/bin/omen-furyctl
	install -D -m 0644 udev/70-omen-fury-wmi.rules \
		$(DESTDIR)/usr/lib/udev/rules.d/70-omen-fury-wmi.rules
	install -D -m 0644 systemd/omen-fury-restore.service \
		$(DESTDIR)/usr/lib/systemd/system/omen-fury-restore.service

install-module: module
	install -D -m 0644 kernel/omen_fury_wmi.ko \
		$(DESTDIR)/lib/modules/$(shell uname -r)/extra/omen_fury_wmi.ko

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/kernel clean
	rm -rf build
