/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 *
 * netd -- network daemon for OpenASD
 *
 * Placeholder network daemon.  On the current hardware model there is
 * no network stack, so netd simply keeps itself alive so svcmgr does
 * not report a launch failure.
 *
 * FIX Bug 5: This binary was referenced by svcmgr as /sbin/netd but
 * did not exist, causing the "FAIL: could not spawn" error at boot.
 */

#include <asd/syscall.h>
#include <asd/stdio.h>
#include <asd/types.h>
#include <stddef.h>
#include <stdint.h>

extern size_t strlen(const char *);

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* Log startup to syslog path if available */
    int fd = asd_open("/var/log/syslog", O_WRONLY | O_APPEND);
    if (fd >= 0) {
        const char *msg = "netd: network daemon started (no hardware)\n";
        asd_write(fd, msg, strlen(msg));
        asd_close(fd);
    }

    /* Keep alive — yield indefinitely */
    for (;;) {
        asd_yield();
    }

    asd_exit(0);
    return 0;
}
