# ASD Init Boot Sequence

## Overview

`asdinit` is PID 1 — the first userland process. It is launched by `kernel_main`
after all kernel subsystems are initialised. It reads service definitions from
`/etc/asdrc/*.svc`, resolves the dependency DAG, and launches services in
parallel where possible.

## Boot Sequence

```
kernel_main()
  │
  ├─ [kernel] memory manager init
  ├─ [kernel] scheduler init
  ├─ [kernel] IPC init
  ├─ [kernel] VFS init
  ├─ [kernel] mount /boot, /root
  ├─ [kernel] driver framework init
  ├─ [kernel] cmdline parse
  │
  └─ spawn /sbin/asdinit  (PID 1, SCHED_INTR, priority 0)
       │
       ├─ Phase 1: Mount remaining filesystems
       │   └─ mount /dev (devfs, synthetic)
       │
       ├─ Phase 2: Parse service definitions
       │   ├─ open /etc/asdrc/ directory
       │   ├─ for each .svc file:
       │   │   ├─ parse service block
       │   │   ├─ validate fields
       │   │   └─ add to service list
       │   └─ build dependency DAG
       │
       ├─ Phase 3: Resolve DAG
       │   ├─ detect cycles → fatal error
       │   ├─ topological sort
       │   └─ compute launch waves (parallel groups)
       │
       ├─ Phase 4: Launch wave 0 (kernel services)
       │   ├─ services with needs: kernel (or no deps)
       │   ├─ spawn in parallel
       │   └─ wait for all to signal ready
       │
       ├─ Phase 5: Launch subsequent waves
       │   └─ repeat until all services running
       │
       ├─ Phase 6: Register signal handlers
       │   ├─ SIGTERM → graceful shutdown
       │   └─ SIGHUP  → reload service definitions
       │
       └─ Enter monitor loop
           ├─ Check restart policies
           │   ├─ on-fail: restart if exit code != 0
           │   ├─ always: restart unconditionally
           │   └─ never: do not restart
           ├─ Report status to actl queries (via port)
           └─ Log events to system ring buffer
```

## Service States

```
              ┌─────────┐
              │  NEW    │  ← just parsed
              └────┬────┘
                   │ spawn
                   ▼
              ┌─────────┐
              │STARTING │  ← process launched, waiting for ready signal
              └────┬────┘
                   │ ready signal received
                   ▼
              ┌─────────┐
         ┌────┤ RUNNING ├────┐
         │    └────┬────┘    │
         │         │ exit    │
         │         ▼         │
         │    ┌─────────┐   │
         │    │ EXITED  │   │
         │    └────┬────┘   │
         │         │        │
         │    restart policy│
         │         │        │
         │    ┌────▼────┐   │
         └────┤RESTARTING├───┘
              └─────────┘
```

## Communication with Services

- Each service gets a dedicated ring buffer (Tier 1 IPC) for log output
- Services can send status updates to asdinit via a message port (Tier 2 IPC)
- asdinit exposes a control port (`/srv/asdinit/ctrl`) for actl queries

## Shutdown Sequence

```
asdinit receives SIGTERM (or keyboard Ctrl+Alt+Del)
  │
  ├─ Phase 1: Stop accepting new services
  ├─ Phase 2: Send SIGTERM to all running services (reverse order of launch)
  ├─ Phase 3: Wait up to 5 seconds for each service to exit
  ├─ Phase 4: SIGKILL any survivors
  ├─ Phase 5: Unmount filesystems (reverse order of mount)
  └─ Phase 6: Call asd_exit(0) → kernel halts
```
