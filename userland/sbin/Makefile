# SPDX-License-Identifier: BSD-2-Clause
# Makefile for /sbin daemons: asdlog, netd

CC := $(shell which clang 2>/dev/null || echo gcc)
LD := $(shell which ld.lld 2>/dev/null || echo ld)

ifeq ($(findstring clang,$(CC)),clang)
  CFLAGS  = --target=x86_64-unknown-elf -nostdlib -nostdinc -ffreestanding \
            -I../libasd/include -I. \
            -I$(shell $(CC) -print-resource-dir)/include -Wno-macro-redefined
  LDFLAGS = -m elf_x86_64 -nostdlib -T ../libasd/link.ld -static
else
  GCC_INC := $(shell gcc -print-file-name=include)
  CFLAGS  = -std=c11 -ffreestanding -fno-stack-protector -fno-builtin \
            -nostdinc -nostdlib -mno-red-zone -O2 -Wall -Wextra \
            -Wno-macro-redefined \
            -I../libasd/include -I. -I$(GCC_INC)
  LDFLAGS = -nostdlib -static -T ../libasd/link.ld
endif

LIBASD    = ../libasd/build/libasd.a
START_OBJ = ../libasd/build/start.o

BINS        = asdlog netd
BIN_TARGETS = $(addprefix build/,$(BINS))

.PHONY: all clean libasd

all: libasd $(BIN_TARGETS)

libasd:
	$(MAKE) -C ../libasd

build:
	mkdir -p build

build/%.o: %.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/%: build/%.o $(LIBASD) $(START_OBJ) | build
	$(LD) $(LDFLAGS) $(START_OBJ) $< $(LIBASD) -o $@

clean:
	rm -rf build
