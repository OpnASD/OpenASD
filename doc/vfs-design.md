# ASD Virtual Filesystem (vfs) — Design Specification

## Overview

ASD's VFS provides a minimal, clean file abstraction without POSIX baggage. It does **not** aim for POSIX compatibility — no `open/read/write/close` names, no `/proc`, no `ioctl`, no file flags like `O_NONBLOCK`.

## Design principles

- **Files are regions**: a file is a named, sized byte sequence with a known offset cursor
- **Everything is a node**: files, directories, and special resources (ring buffers, ports) are all `vfsnode_t`
- **Filesystem drivers register as backends**: the VFS routes operations to the correct backend by mount prefix
- **No VFS-layer caching**: backends manage their own buffers; VFS only handles path resolution and node lifetime
- **Ring buffers exposed as files**: drivers expose Tier 1 ring buffers as virtual files under `/drv/`, readable via `asd_read`

## Path conventions

| Path prefix | Meaning |
|-------------|---------|
| `/boot/`    | Boot filesystem (EFI system partition) |
| `/root/`    | Root filesystem (ASD's native fs) |
| `/drv/`     | Virtual driver nodes (ring buffers, status pages) |
| `/shm/`     | Shared memory name space (maps to `asd_shmap`) |
| `/sys/`     | System information (read-only, synthetic) |

Paths are always absolute in kernel VFS. Userland `asd_open` resolves relative paths against a per-process cwd stored in the PCB.

## Data structures

```
vfsnode_t (one per open file)
┌─────────────────────────────┐
│ node_id   (uint32_t)        │  0 = free slot
│ refcount  (uint32_t)        │
│ flags     (uint32_t)        │  VFS_F_* constants
│ offset    (uint64_t)        │  current read/write cursor
│ size      (int64_t)         │  file size (-1 = unknown/stream)
│ path      (char[256])       │  resolved absolute path
│ backend   (vfs_backend_t*)  │  NULL for synthetic nodes
│ bctx      (void*)           │  backend-private context
│ pcb       (pcb_t*)          │  owning process
│ ring      (ringbuf_t*)      │  non-NULL if this is a ring-buffer node
│ next      (vfsnode_t*)      │  intrusive list
└─────────────────────────────┘

vfs_backend_t (per-mounted filesystem or driver)
┌─────────────────────────────┐
│ name      (char[64])        │  backend identifier
│ mount_pt  (char[256])       │  mount prefix (e.g. "/root")
│ ops       (vfs_ops_t*)      │  function pointers
│ ctx       (void*)           │  backend-private state
└─────────────────────────────┘

vfs_ops_t (backend operations)
┌─────────────────────────────┐
│ open      (path, flags) → bctx  │
│ close     (bctx)                │
│ read      (bctx, buf, n, *got)  │
│ write     (bctx, buf, n)        │
│ seek      (bctx, off, whence)   │
│ readdir   (bctx, buf, cap, *n)  │  directory listing
│ stat      (bctx, *info)         │  size, kind, timestamps
│ unlink    (path)                │
│ mkdir     (path)                │
└─────────────────────────────┘
```

## File descriptor convention

File descriptors are small non-negative integers starting at 1 (0 is reserved/invalid). They index into a per-process fd table:

```
pcb_t.fd_table[MAX_FDS]
┌──────┬──────────────┐
│ fd   │ vfsnode_t*   │
├──────┼──────────────┤
│  1   │ ──────────┐  │
│  2   │           │  │  (stdin/stdout/stderr reserved, not used by ASD)
│  3   │ ──────┐   │  │
│ ...  │       │   │  │
└──────┤       │   │  │
       ▼       ▼   ▼  │
     node_t  node_t   │
                     │
```

Per-process fd table: `fd_slot_t fds[MAX_FDS]` where each slot holds a `vfsnode_t*` pointer.

## Syscalls

| Syscall | Signature | Description |
|---------|-----------|-------------|
| `asd_open` | `(const char *path, int flags) → fd_t` | Open a file; returns fd |
| `asd_close` | `(fd_t fd) → int` | Close a file descriptor |
| `asd_read` | `(fd_t fd, void *buf, size_t n, size_t *got) → int` | Read bytes, advance offset |
| `asd_write` | `(fd_t fd, const void *buf, size_t n) → int` | Write bytes, advance offset |
| `asd_seek` | `(fd_t fd, int64_t off, int whence) → uint64_t` | Seek; returns new offset |
| `asd_stat` | `(const char *path, stat_info_t *out) → int` | Get file metadata without opening |
| `asd_unlink` | `(const char *path) → int` | Delete a file or empty directory |
| `asd_mkdir` | `(const char *path) → int` | Create a directory |

### Flags for `asd_open`

| Constant | Value | Meaning |
|----------|-------|---------|
| `VFS_O_READ` | `0x01` | Read access |
| `VFS_O_WRITE` | `0x02` | Write access |
| `VFS_O_CREAT` | `0x04` | Create if not exists |
| `VFS_O_TRUNC` | `0x08` | Truncate to zero on open |
| `VFS_O_DIR` | `0x10` | Open as directory (for readdir) |

### Whence for `asd_seek`

| Constant | Value | Meaning |
|----------|-------|---------|
| `VFS_SEEK_SET` | `0` | From beginning of file |
| `VFS_SEEK_CUR` | `1` | From current offset |
| `VFS_SEEK_END` | `2` | From end of file |

### stat_info_t

```c
typedef struct {
    int64_t  size;       /* file size in bytes, -1 if unknown */
    uint8_t  kind;       /* VFS_NODE_FILE, VFS_NODE_DIR, VFS_NODE_RING */
    uint64_t mtime_ns;   /* last modification time */
    char     name[256];  /* base name */
} stat_info_t;
```

## Path resolution

Path resolution is a simple prefix walk:

```
asd_open("/root/usr/bin/dctl.adf", VFS_O_READ)
  1. Strip trailing slashes
  2. Match longest mount prefix: "/root" → backend "rootfs"
  3. Pass remainder "/usr/bin/dctl.adf" to backend's open()
  4. Backend returns bctx (e.g. inode number + cached buffer)
  5. VFS allocates vfsnode_t, stores bctx, returns fd
```

For synthetic paths (`/drv/`, `/sys/`, `/shm/`), the VFS handles resolution internally without a backend.

## Synthetic nodes

Synthetic nodes are created on-the-fly by the VFS itself:

| Path pattern | Node type | Backend ops |
|--------------|-----------|-------------|
| `/drv/<name>/rx` | Ring buffer | read → ringbuf_pop, write → ringbuf_push |
| `/drv/<name>/tx` | Ring buffer | read → ringbuf_pop, write → ringbuf_push |
| `/drv/<name>/ctrl` | Message port | read → port_recv, write → port_send |
| `/drv/<name>/status` | Shared memory | read → memcpy from status page |
| `/sys/cpu` | Synthetic file | read → format sched_state |
| `/sys/mem` | Synthetic file | read → format phys allocator stats |
| `/sys/uptime` | Synthetic file | read → current monotonic clock |

## Boot filesystem

`/boot` is backed by the EFI System Partition. At boot time, `asdboot` already parsed it to load the kernel. After boot, a simple FAT16/32 reader provides access to `/boot/asdboot.conf` and other boot config files.

For initial bring-up, `/boot` can be a **ramfs** pre-populated by the UKI stub with the contents of the `.cmdline` section and any boot config.

## Root filesystem

ASD's native filesystem format is **not** ext4, not FAT, not UFS. For initial bring-up we implement a simple **ASDF** (ASD Filesystem) — a minimal, append-only, extent-based filesystem with:

- Fixed-size superblock at sector 0
- Inode table at sector 1
- Data area with extent allocation (contiguous block groups)
- No journaling (added later)

For now, the rootfs backend is a **stub**: it reads from a pre-loaded buffer (embedded in the UKI or provided by the bootloader). This is sufficient to load driver modules from the initrd during early boot.

## Thread safety

- Global `vfs_lock` protects the node table and backend list
- Per-backend locking is the backend's responsibility
- `vfsnode_t.refcount` is atomic
- fd table is per-process (no lock needed for fd lookup)

## Error codes

All VFS syscalls return `0` on success, negative on error:

| Code | Name | Meaning |
|------|------|---------|
| `-1` | `VFS_ERR` | Generic error |
| `-2` | `VFS_ENOENT` | Path not found |
| `-3` | `VFS_EINVAL` | Invalid argument |
| `-4` | `VFS_EPERM` | Permission denied |
| `-5` | `VFS_ENOSPC` | No space left |
| `-6` | `VFS_ENODEV` | No such backend |
| `-7` | `VFS_EISDIR` | Is a directory |
| `-8` | `VFS_ENOTDIR` | Not a directory |
| `-9` | `VFS_EMFILE` | Too many open files |
