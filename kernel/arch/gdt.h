/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 */

#ifndef ASD_GDT_H
#define ASD_GDT_H

#include <stdint.h>

/* Segment selectors */
#define SEG_NULL         0x00
#define SEG_KERNEL_CODE  0x08
#define SEG_KERNEL_DATA  0x10
#define SEG_USER_CODE    0x18   /* use 0x1B (|3) for ring-3 */
#define SEG_USER_DATA    0x20   /* use 0x23 (|3) for ring-3 */
#define SEG_TSS          0x28   /* 16-byte TSS descriptor */

void gdt_init(void);
void tss_set_rsp0(uint64_t rsp0);

#endif /* ASD_GDT_H */
