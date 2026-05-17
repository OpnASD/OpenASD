# SPDX-License-Identifier: BSD-2-Clause
# Makefile for asded — ASD text editor

CC      = clang
CFLAGS  = --target=x86_64-unknown-elf \
          -ffreestanding \
          -fno-stack-protector \
          -fno-builtin \
          -mno-red-zone \
          -nostdlib \
          -nostdinc \
          -Os \
          -Wall \
          -Wextra \
          -Wno-macro-redefined \
          -I../libasd/include \
          -I$(shell $(CC) -print-resource-dir)/include

LD      = ld.lld
LDFLAGS = -m elf_x86_64 \
          -nostdlib \
          -static \
          -T ../libasd/link.ld

BUILD   = build
TARGET  = $(BUILD)/asded
LIBASD  = ../libasd/build/libasd.a
START   = ../libasd/build/start.o

OBJS    = $(BUILD)/asded.o

.PHONY: all clean

all: $(TARGET)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): $(OBJS) $(LIBASD) $(START)
	$(LD) $(LDFLAGS) -o $@ $(START) $(OBJS) $(LIBASD)

clean:
	rm -rf $(BUILD)
