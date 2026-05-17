# ASD Glossary

All invented names used across the ASD project.  
When a new name is coined anywhere in the codebase, it must be added here first.

---

## Subsystems

| Name        | Role                                      |
|-------------|-------------------------------------------|
| `asdboot`   | Bootloader                                |
| `asduki`    | Unified Kernel Image builder/format       |
| `asdhed`    | Kernel scheduler ("hedger")               |
| `amm`       | ASD Memory Manager                        |
| `adf`       | ASD Driver Framework                      |
| `asdinit`   | PID 1 init process                        |
| `asdrc`     | Service definition subsystem (read by asdinit) |
| `asdsh`     | ASD shell                                 |
| `asdfs`     | ASD native filesystem (ASDF)              |
| `vfs`       | Virtual Filesystem layer                  |
| `asdinstall`| TUI installer                             |

---

## Syscalls

Syscalls follow the convention `asd_<verb>_<noun>` or `asd_<verb>` for simple
operations. All take explicit typed arguments; no variadic syscalls.

| Syscall                                              | Description                                              |
|------------------------------------------------------|----------------------------------------------------------|
| `asd_spawn(spawn_args_t *)`                          | Create a new process; returns pid_t                      |
| `asd_reap(pid_t pid, exit_info_t *out)`              | Collect exit status of a finished process                |
| `asd_exit(int code)`                                 | Terminate calling process                                |
| `asd_sched_set(pid_t pid, sched_class_t cls, int pri)` | Set scheduling class and priority                      |
| `asd_yield()`                                        | Voluntarily yield CPU                                    |
| `asd_sleep(uint64_t ns)`                             | Sleep for nanoseconds                                    |
| `asd_shmap(const char *name, size_t sz, int flags)`  | Create or open a named shared memory region; returns vaddr |
| `asd_shunmap(vaddr_t addr)`                          | Unmap a shared region from the calling process           |
| `asd_region(vaddr_t base, size_t sz, int prot, int kind)` | Register an explicit VA region (code/stack/heap/mapped) |
| `asd_unregion(vaddr_t base)`                         | Remove a VA region                                       |
| `asd_port_open(const char *name, int flags)`         | Open or create a message port; returns port_t            |
| `asd_port_close(port_t p)`                           | Close a message port                                     |
| `asd_port_send(port_t p, const void *msg, size_t len)` | Send message (Tier 2 IPC)                              |
| `asd_port_recv(port_t p, void *buf, size_t cap, size_t *len_out)` | Receive message (Tier 2 IPC)            |
| `asd_mod_load(const char *path)`                     | Load a driver module (.adf file)                         |
| `asd_mod_unload(mod_id_t id)`                        | Unload a driver module                                   |
| `asd_time(uint64_t *ns_out)`                         | Read monotonic clock (nanoseconds)                       |
| `asd_mapfile(int fd, vaddr_t hint, size_t sz, int prot)` | Map a file into the VA space                         |
| `asd_open(const char *path, int flags)`              | Open a file; returns fd_t                                |
| `asd_close(fd_t fd)`                                 | Close a file descriptor                                  |
| `asd_read(fd_t fd, void *buf, size_t n, size_t *got)`| Read bytes                                               |
| `asd_write(fd_t fd, const void *buf, size_t n)`      | Write bytes                                              |
| `asd_seek(fd_t fd, int64_t off, int whence)`         | Seek within a file                                       |
| `asd_stat(const char *path, stat_info_t *out)`        | Get file metadata without opening                        |
| `asd_unlink(const char *path)`                        | Delete a file or empty directory                         |
| `asd_mkdir(const char *path)`                         | Create a directory                                       |

---

## VFS Constants

| Name              | Value      | Description                                    |
|-------------------|------------|------------------------------------------------|
| `VFS_O_READ`      | `0x01`     | Open for reading                                 |
| `VFS_O_WRITE`     | `0x02`     | Open for writing                                 |
| `VFS_O_CREAT`     | `0x04`     | Create if not exists                             |
| `VFS_O_TRUNC`     | `0x08`     | Truncate to zero on open                         |
| `VFS_O_DIR`       | `0x10`     | Open as directory                                |
| `VFS_SEEK_SET`    | `0`        | Seek from beginning of file                      |
| `VFS_SEEK_CUR`    | `1`        | Seek from current offset                         |
| `VFS_SEEK_END`    | `2`        | Seek from end of file                            |
| `VFS_NODE_FILE`   | `1`        | Regular file node                                |
| `VFS_NODE_DIR`    | `2`        | Directory node                                   |
| `VFS_NODE_RING`   | `3`        | Ring buffer node (synthetic)                     |
| `VFS_NODE_PORT`   | `4`        | Message port node (synthetic)                    |
| `VFS_NODE_SHM`    | `5`        | Shared memory node (synthetic)                   |
| `MAX_FDS`         | `256`      | Maximum open files per process                   |
| `MAX_VFSNODES`    | `1024`     | Maximum open files system-wide                   |
| `MAX_BACKENDS`    | `16`       | Maximum mounted filesystems / backends           |
| `VFS_PATH_MAX`    | `256`      | Maximum path length                              |

---

## Scheduling Classes

| Name          | Constant        | Description                                         |
|---------------|-----------------|-----------------------------------------------------|
| Realtime      | `SCHED_RT`      | Fixed priority, never preempted by lower class      |
| Interactive   | `SCHED_INTR`    | Dynamic priority, boosted on I/O wakeup             |
| Batch         | `SCHED_BATCH`   | Lowest priority, CPU-bound work                     |

---

## Memory Region Kinds

| Name           | Constant      | Description                           |
|----------------|---------------|---------------------------------------|
| Code           | `RGN_CODE`    | Executable, read-only after map       |
| Stack          | `RGN_STACK`   | Grows down, guard page at bottom      |
| Heap           | `RGN_HEAP`    | Explicitly sized, no implicit growth  |
| Mapped file    | `RGN_FILE`    | Backed by a file descriptor           |
| Shared         | `RGN_SHARED`  | Shared memory from asd_shmap          |

---

## IPC Tiers

| Tier | Name              | Kernel involvement  |
|------|-------------------|---------------------|
| 0    | Shared region     | Only at map time    |
| 1    | Ring buffer       | None after map      |
| 2    | Message port      | Every send/recv     |

---

## Shell Commands

All commands are part of the `asdsh` command suite.  
Option style: `:name value` (colon-prefix named options).  
See `doc/asdsh-syntax.md` for the full grammar.

| Command   | Equivalent function          | Example                              |
|-----------|------------------------------|--------------------------------------|
| `ls`      | Directory listing            | `ls /usr/bin`                        |
| `cp`      | Copy file or directory       | `cp :from a.txt :to b.txt`           |
| `mv`      | Move / rename                | `mv :from old :to new`               |
| `del`     | Delete file or directory     | `del :path /tmp/foo`                 |
| `show`    | Display text file            | `show /etc/hostname`                 |
| `procs`   | List processes               | `procs`                              |
| `halt`    | Terminate a process          | `halt :pid 42`                       |
| `mnt`     | Mount a filesystem           | `mnt :dev /dev/disk0 :at /mnt/usb`  |
| `umnt`    | Unmount a filesystem         | `umnt :at /mnt/usb`                  |
| `netstat` | Network interface status     | `netstat`                            |
| `seek`    | Search text in files         | `seek :pat "error" :in /var/log`    |
| `arc`     | Create archive               | `arc :make out.asar :add /src`      |
| `unarc`   | Extract archive              | `unarc :file out.asar :to /dst`     |
| `env`     | Show/set environment vars    | `env :set PATH /usr/bin`            |
| `cd`      | Change working directory     | `cd /usr`                           |
| `pwd`     | Print working directory      | `pwd`                               |
| `echo`    | Print arguments              | `echo hello world`                  |
| `which`   | Locate a command binary      | `which ls`                          |
| `read`    | Read a line from stdin       | `read :into name`                   |

Note: `cd`, `pwd`, `echo`, `read` are shell built-ins, not external binaries.

---

## Init / Service Management

| Name      | Role                                             |
|-----------|--------------------------------------------------|
| `asdinit` | PID 1; reads asdrc, launches services            |
| `asdrc`   | Service definition language and /etc/asdrc/ dir  |
| `actl`    | CLI tool: start / stop / restart / status        |

`actl` command syntax (follows ASD option style):

```
actl start  :svc syslog
actl stop   :svc syslog
actl restart :svc syslog
actl status :svc syslog
actl list
```

---

## Driver Management

| Name   | Role                                        |
|--------|---------------------------------------------|
| `dctl` | Driver manager CLI (wraps asd_mod_load etc) |

`dctl` command syntax:

```
dctl list
dctl load   :path /lib/drv/e1000.adf
dctl unload :name net_e1000
dctl status :name net_e1000
dctl probe
```

---

## Config / Format Names

| Name              | Location              | Format                      |
|-------------------|-----------------------|-----------------------------|
| `asdboot.conf`    | `/boot/asdboot.conf`  | ASD block-config (see asdboot-conf.md) |
| `asdrc` service   | `/etc/asdrc/*.svc`    | ASD script (see asd-script.md) |
| `.adf` module     | `/lib/drv/*.adf`      | ELF + embedded manifest     |
| `.asar` archive   | any path              | ASD archive format          |

---

## Type Aliases (C)

| Type          | Underlying   | Description                     |
|---------------|--------------|---------------------------------|
| `pid_t`       | `uint32_t`   | Process identifier              |
| `port_t`      | `uint32_t`   | Message port handle             |
| `mod_id_t`    | `uint32_t`   | Loaded module identifier        |
| `vaddr_t`     | `uintptr_t`  | Virtual address                 |
| `paddr_t`     | `uintptr_t`  | Physical address                |
| `fd_t`          | `uint32_t` | File descriptor (≥1, 0 invalid) |
| `sched_class_t` | `uint8_t`  | Scheduling class enum           |
