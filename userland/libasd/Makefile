# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026, ASD Project Contributors
#
# libasd — freestanding C library for ASD userland (x86-64 bare-metal ELF)

CC     = clang
AS     = clang
CFLAGS = --target=x86_64-unknown-elf \
         -ffreestanding \
         -fno-stack-protector \
         -fno-builtin \
         -mno-red-zone \
         -mgeneral-regs-only \
         -nostdlib \
         -nostdinc \
         -Iinclude \
         -I$(shell clang -print-resource-dir)/include \
         -Os -Wall -Wextra -Wno-unused-parameter \
          -Wno-macro-redefined

ASFLAGS = --target=x86_64-unknown-elf -ffreestanding -c

BUILD = build
LIB   = $(BUILD)/libasd.a

OBJS  = $(BUILD)/start.o \
        $(BUILD)/syscall_asm.o \
        $(BUILD)/stdio.o \
        $(BUILD)/string.o

.PHONY: all clean

all: $(LIB)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/start.o: start.S | $(BUILD)
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD)/syscall_asm.o: syscall_asm.S | $(BUILD)
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD)/stdio.o: stdio.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/string.o: string.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(LIB): $(OBJS)
	ar rcs $@ $(OBJS)

clean:
	rm -rf $(BUILD)
