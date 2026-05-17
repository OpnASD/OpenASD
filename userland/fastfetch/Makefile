# SPDX-License-Identifier: MIT
# Makefile for fastfetch — OpenASD userland port (minimal link, no libasd stdio)

CC      = clang
CFLAGS  = --target=x86_64-unknown-elf \
          -ffreestanding \
          -fno-stack-protector \
          -fno-builtin \
          -mno-red-zone \
          -mgeneral-regs-only \
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
TARGET  = $(BUILD)/fastfetch
START   = ../libasd/build/start.o
SYSCALL = ../libasd/build/syscall_asm.o

OBJS    = $(BUILD)/fastfetch.o

.PHONY: all clean

all: $(TARGET)

$(START) $(SYSCALL):
	$(MAKE) -C ../libasd

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): $(OBJS) $(START) $(SYSCALL)
	$(LD) $(LDFLAGS) -o $@ $(START) $(OBJS) $(SYSCALL)

clean:
	rm -rf $(BUILD)
