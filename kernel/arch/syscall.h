/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 *
 * ASD syscall numbers and kernel-side declarations.
 *
 * The ABI follows the Linux x86-64 syscall convention so that
 * ported programs need minimal stub work:
 *   rax = syscall number
 *   rdi, rsi, rdx, r10, r8, r9 = up to 6 arguments
 *   Return value in rax (negative = errno negated).
 */

#ifndef ASD_SYSCALL_H
#define ASD_SYSCALL_H

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Syscall numbers                                                      */
/* ------------------------------------------------------------------ */

/* Core I/O */
#define SYS_EXIT      1
#define SYS_OPEN      2
#define SYS_CLOSE     3
#define SYS_READ      4
#define SYS_WRITE     5
#define SYS_STAT      6
#define SYS_SEEK      7
#define SYS_MKDIR     8
#define SYS_UNLINK    9
#define SYS_READDIR   10

/* Process management */
#define SYS_SPAWN     11
#define SYS_WAIT      12
#define SYS_GETPID    13
#define SYS_YIELD     14
#define SYS_TIME      15

/* Identity */
#define SYS_GETUID    16
#define SYS_GETGID    17
#define SYS_GETEUID   18
#define SYS_GETEGID   19
#define SYS_GETPPID   20

/* System info */
#define SYS_UNAME     21

/* Memory management */
#define SYS_MMAP      22
#define SYS_MUNMAP    23
#define SYS_BRK       24

/* Terminal / misc */
#define SYS_IOCTL     25
#define SYS_WRITEV    26

/* Extended syscalls (v5) */
#define SYS_GETTIME_NS 27  /* monotonic nanoseconds since boot */
#define SYS_SETUID    28   /* set real UID (root-only for arbitrary change) */
#define SYS_SETGID    29   /* set real GID */

#define SYSCALL_MAX   29

/* ------------------------------------------------------------------ */
/* Open flags (match VFS flags) */
/* ------------------------------------------------------------------ */
#define O_RDONLY   0x01
#define O_WRONLY   0x02
#define O_RDWR     0x03
#define O_CREAT    0x10
#define O_TRUNC    0x20
#define O_APPEND   0x40
#define O_DIRECTORY 0x80

/* ------------------------------------------------------------------ */
/* Seek whence values */
/* ------------------------------------------------------------------ */
#define SEEK_SET   0
#define SEEK_CUR   1
#define SEEK_END   2

/* ------------------------------------------------------------------ */
/* mmap flags */
/* ------------------------------------------------------------------ */
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#ifndef PROT_NONE
#define PROT_NONE     0x00
#endif
#ifndef PROT_READ
#define PROT_READ     0x01
#endif
#ifndef PROT_WRITE
#define PROT_WRITE    0x02
#endif
#ifndef PROT_EXEC
#define PROT_EXEC     0x04
#endif

/* ------------------------------------------------------------------ */
/* Kernel-side API                                                      */
/* ------------------------------------------------------------------ */

/* Set up SYSCALL/SYSRET MSRs — call after GDT is loaded */
void syscall_init(void);

/* Kernel RSP for fault trampoline (see idt.c irq_user_fault_exit). */
uint64_t syscall_kernel_rsp0(void);

/* Point SYSCALL/IRQ kernel stack at the given thread's kstack. */
struct pcb;
void syscall_on_thread_switch(struct pcb *pcb);

/* Entry point called from syscall_entry.S */
uint64_t syscall_dispatch(uint64_t nr,
                          uint64_t a0, uint64_t a1, uint64_t a2,
                          uint64_t a3, uint64_t a4);

#endif /* ASD_SYSCALL_H */
