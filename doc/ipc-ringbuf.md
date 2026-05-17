# ASD IPC — Ring Buffer Protocol

Version: 1.0  
Source: `kernel/ipc/`

---

## 1. IPC Philosophy

ASD's IPC is tiered to avoid syscall overhead on the hot path:

| Tier | Name            | Kernel on each op | Use case                          |
|------|-----------------|-------------------|-----------------------------------|
| 0    | Shared region   | No (only at map)  | Scheduler state, config pages     |
| 1    | Ring buffer     | No                | Driver↔userland data streams      |
| 2    | Message port    | Yes               | Control messages, process signals |

Tier 0 and Tier 1 operate entirely in userland once the shared memory region
is established by `asd_shmap` (one syscall). Tier 2 involves a syscall per
send/recv.

---

## 2. Tier 0 — Shared Region

A named shared memory region is created once with `asd_shmap`, then any
process that calls `asd_shmap` with the same name receives a mapping of the
same physical pages. From that point on, both sides read/write without any
kernel involvement.

```c
/* Producer */
vaddr_t state = asd_shmap("myapp.state", 4096, SHM_CREATE);
((my_state_t *)state)->counter++;

/* Consumer (different process) */
vaddr_t state = asd_shmap("myapp.state", 4096, 0);
uint64_t v = ((my_state_t *)state)->counter;
```

No synchronisation is provided by the kernel. Producers and consumers must
use atomic operations or the Tier 1 ring buffer for ordering guarantees.

---

## 3. Tier 1 — Lock-Free Ring Buffer

A lock-free single-producer / single-consumer (SPSC) ring buffer embedded in
a shared memory region. Multiple producers or consumers require external
locking around `ringbuf_push` / `ringbuf_pop` — the ring itself is SPSC.

### 3.1 Memory Layout

The ring buffer occupies a contiguous shared region. Its layout:

```
Offset  Size   Field
──────  ─────  ──────────────────────────────────────────────────────────
0       8      magic     = 0x52494E47_42554631  ("RINGBUF1")
8       4      version   = 1
12      4      slot_size  (bytes per slot, power of 2, min 8)
16      4      slot_count (number of slots, power of 2)
20      4      _pad
24      8      write_idx  (producer writes here; monotonic)
32      8      read_idx   (consumer reads here; monotonic)
40      24     _reserved
64      ...    data[]     (slot_count * slot_size bytes)
```

Total header size: 64 bytes (one cache line).

The ring occupies `64 + slot_count * slot_size` bytes and must be allocated
via `asd_shmap` with at least that size.

### 3.2 Index arithmetic

```
actual_slot = idx & (slot_count - 1)   /* modulo via bitmask */
```

`write_idx` and `read_idx` are monotonically increasing 64-bit counters.
The ring is full when `write_idx - read_idx == slot_count`.
The ring is empty when `write_idx == read_idx`.

### 3.3 Push (producer)

```c
int ringbuf_push(ringbuf_t *rb, const void *data, uint32_t size) {
    /* size must be <= slot_size */
    uint64_t widx = __atomic_load_n(&rb->write_idx, __ATOMIC_RELAXED);
    uint64_t ridx = __atomic_load_n(&rb->read_idx,  __ATOMIC_ACQUIRE);
    if (widx - ridx == rb->slot_count) return -1; /* full */

    void *slot = rb->data + (widx & (rb->slot_count - 1)) * rb->slot_size;
    memcpy(slot, data, size);

    __atomic_store_n(&rb->write_idx, widx + 1, __ATOMIC_RELEASE);
    return 0;
}
```

### 3.4 Pop (consumer)

```c
int ringbuf_pop(ringbuf_t *rb, void *buf, uint32_t size) {
    uint64_t ridx = __atomic_load_n(&rb->read_idx,  __ATOMIC_RELAXED);
    uint64_t widx = __atomic_load_n(&rb->write_idx, __ATOMIC_ACQUIRE);
    if (ridx == widx) return -1; /* empty */

    const void *slot = rb->data + (ridx & (rb->slot_count - 1)) * rb->slot_size;
    memcpy(buf, slot, size);

    __atomic_store_n(&rb->read_idx, ridx + 1, __ATOMIC_RELEASE);
    return 0;
}
```

### 3.5 Memory ordering

- Producer writes data **before** `RELEASE` store of `write_idx`.
- Consumer reads `write_idx` with `ACQUIRE` before reading data.
- This is the standard SPSC ring buffer ordering on TSO architectures (x86-64).
  No explicit memory barriers beyond the `ACQUIRE`/`RELEASE` atomics are needed.

### 3.6 Batch operations

For throughput-sensitive paths, producers and consumers may batch:

```c
/* Push N items without intermediate index stores */
uint64_t widx = __atomic_load_n(&rb->write_idx, __ATOMIC_RELAXED);
uint64_t ridx = __atomic_load_n(&rb->read_idx,  __ATOMIC_ACQUIRE);
uint32_t avail = rb->slot_count - (uint32_t)(widx - ridx);
uint32_t n = (count < avail) ? count : avail;
for (uint32_t i = 0; i < n; i++) {
    memcpy(slot(rb, widx + i), items[i], rb->slot_size);
}
__atomic_store_n(&rb->write_idx, widx + n, __ATOMIC_RELEASE);
```

---

## 4. Tier 2 — Message Port

Message ports are kernel objects. Each port has a name, a send queue, and a
receive queue. Only one process may call `asd_port_recv` on a port at a time
(the owning process).

### 4.1 Creating a port

```c
port_t p = asd_port_open("myapp.control", PORT_CREATE);
```

### 4.2 Sending a message

```c
typedef struct { uint32_t type; uint32_t len; uint8_t data[56]; } msg_t;
msg_t m = { .type = 1, .len = 4 };
asd_port_send(p, &m, sizeof(m));   /* blocks if port queue is full */
```

### 4.3 Receiving a message

```c
msg_t m;
size_t got;
asd_port_recv(p, &m, sizeof(m), &got);   /* blocks until message available */
```

### 4.4 Port object layout (kernel internal)

```
port_obj_t
┌──────────────────────────────────────────────────┐
│ name[64]     char      null-terminated           │
│ owner_pid    pid_t     owning process            │
│ lock         spinlock                            │
│ queue        ringbuf_t embedded in kmalloc buf   │
│ waiter       pcb_t *   process blocked on recv   │
│ refcount     uint32                              │
└──────────────────────────────────────────────────┘
```

The port queue is a Tier 1 ring buffer allocated in kernel heap memory.
Messages are copied in/out on each syscall — no zero-copy at Tier 2.

### 4.5 Message size limits

- Minimum: 8 bytes
- Maximum: 4096 bytes (larger transfers should use Tier 0/1)
- Port queue depth: 64 messages (configurable at port creation, max 4096)

---

## 5. Driver ↔ Userland Pattern

The canonical pattern for a driver exposing data to userland:

```
Kernel driver                      Userland consumer
─────────────────                  ─────────────────
asd_shmap("drv.net0.rx", ...)     asd_shmap("drv.net0.rx", ...)
  → maps ring buffer region          → maps same region

[on packet receive]
  ringbuf_push(&rx_ring, pkt)

                                   while (1)
                                     ringbuf_pop(&rx_ring, buf)
                                     process(buf)
```

Tier 2 is used for control:
```
Userland                           Kernel driver
────────────────                   ───────────────
asd_port_send(ctrl_port,           asd_port_recv blocks...
  {type=SET_MTU, mtu=9000})          → wakes, processes command
```

No ioctl. No `/dev` node read/write.

---

## 6. Initialising a Ring Buffer

The helper `ringbuf_init` is provided in `kernel/ipc/ringbuf.h`:

```c
int ringbuf_init(void *mem, uint32_t slot_size, uint32_t slot_count);
```

Requirements:
- `mem` must point to at least `64 + slot_count * slot_size` bytes.
- `slot_size` must be a power of 2, minimum 8.
- `slot_count` must be a power of 2, minimum 2.

Returns 0 on success, -1 on invalid arguments.
