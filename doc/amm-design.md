# amm — Memory Manager Design

Version: 1.0  
Source: `kernel/mm/`

---

## 1. Overview

`amm` (ASD Memory Manager) is the kernel memory subsystem. It consists of:

1. **Physical allocator** — buddy system (see `doc/amm-physical.md`)
2. **Virtual address map** (`vmap_t`) — per-process region list + page tables
3. **Shared memory** (`asd_shmap`) — named regions mapped into multiple processes
4. **Demand pager** — fills pages on `#PF` fault
5. **Kernel heap** — slab allocator over the buddy system

---

## 2. Virtual Address Space Layout (per process)

```
Virtual Address Space (x86-64, 48-bit)

0x0000_0000_0000_0000  ┌──────────────────────────────┐
                       │  Null guard (unmapped)        │  1 page
0x0000_0000_0000_1000  ├──────────────────────────────┤
                       │  User code  (RGN_CODE)        │
                       │  User data                    │
                       │  User heap  (RGN_HEAP)        │
                       │  Memory-mapped files          │
                       │  Shared regions (RGN_SHARED)  │
0x0000_7FFF_FFFF_0000  ├──────────────────────────────┤
                       │  User stack (RGN_STACK)       │  grows down
                       │  Stack guard (unmapped)       │  1 page
0x0000_8000_0000_0000  ├──────────────────────────────┤
                       │  (non-canonical hole)         │
0xFFFF_8000_0000_0000  ├──────────────────────────────┤
                       │  page_info array (kernel)     │  128 MiB
0xFFFF_8800_0000_0000  ├──────────────────────────────┤
                       │  Kernel heap (kmalloc)        │  512 GiB
0xFFFF_C800_0000_0000  ├──────────────────────────────┤
                       │  Physical identity map        │  all physical RAM
0xFFFF_E800_0000_0000  ├──────────────────────────────┤
                       │  Kernel shared regions        │  (asd_shmap kernel side)
0xFFFF_F000_0000_0000  ├──────────────────────────────┤
                       │  Kernel stacks (per-CPU, per-thread) │
0xFFFF_FF00_0000_0000  ├──────────────────────────────┤
                       │  Kernel image                 │  linked at 0xFFFF_FF80_0000_0000
0xFFFF_FFFF_FFFF_FFFF  └──────────────────────────────┘
```

---

## 3. Region Descriptor List

Each `vmap_t` holds a doubly-linked, address-sorted list of `vregion_t` objects.
Regions may not overlap. Gaps between regions are unmapped.

```
vmap_t
 ├─ head → vregion_t[0x1000, 0x5000, RGN_CODE, r-x]
 │           └─ next → vregion_t[0x5000, 0x1000, RGN_HEAP, rw-]
 │                      └─ next → vregion_t[0x7FFF_0000, 0x10000, RGN_STACK, rw-]
 │                                  └─ next → NULL
 ├─ cr3 → (physical PML4 address)
 └─ region_count = 3
```

Regions are **explicitly registered** by the process via `asd_region()`.
There is no implicit heap growth (`brk`-style). The process must call
`asd_region()` to expand the heap, then manage its own allocator within that
region.

---

## 4. Demand Paging

Physical pages are not allocated when a region is registered. They are allocated
lazily on the first access (#PF with NOT_PRESENT bit set).

```
#PF handler
    │
    ├─ Read fault address (cr2)
    │
    ├─ Find vregion containing fault_addr
    │   └─ If not found → SEGFAULT → deliver signal / kill process
    │
    ├─ Check access type vs region prot
    │   └─ Write to r-x region → SEGFAULT
    │
    ├─ Allocate physical page: amm_phys_alloc(order=0)
    │
    ├─ Zero the page (security: no information leakage)
    │
    ├─ Map page into PML4 hierarchy:
    │     walk PML4 → PDPT → PD → PT
    │     allocate intermediate tables as needed
    │     set PTE: present | writable(if rw) | user | no_exec(if !rx)
    │
    └─ Return from #PF handler (retry faulting instruction)
```

### Copy-on-write (future)

Not implemented in v1.0. All memory is private. Shared regions (RGN_SHARED)
use a reference-counted physical page and are never CoW.

---

## 5. Shared Memory (`asd_shmap`)

Named shared regions are the primary IPC Tier 0 mechanism. See `doc/ipc-ringbuf.md`.

### Data structures

```
shm_entry_t (kernel global table)
┌──────────────────────────────────────────────────┐
│ name[64]     char     null-terminated name       │
│ paddr        paddr_t  first physical page        │
│ size         size_t   total size (page-aligned)  │
│ refcount     uint32   number of active mappings  │
│ flags        uint32   SHM_* flags                │
└──────────────────────────────────────────────────┘

shm_table: shm_entry_t[MAX_SHM_ENTRIES]  (MAX_SHM_ENTRIES = 256)
```

### `amm_shmap` flow

```
amm_shmap(name, size, flags)
    │
    ├─ Lock shm_table
    │
    ├─ Search for existing entry with matching name
    │   ├─ Found: check size compatibility; increment refcount
    │   └─ Not found (and SHM_CREATE set):
    │         allocate contiguous pages (amm_phys_alloc)
    │         create new shm_entry_t
    │
    ├─ Map the physical pages into the calling process's VA map:
    │     choose a VA hint in the RGN_SHARED zone
    │     call amm_region_add(map, va, size, prot, RGN_SHARED, -1)
    │     walk page tables, map each page immediately (not demand-paged)
    │
    └─ Return VA
```

---

## 6. Page Table Management

ASD uses standard x86-64 four-level paging: PML4 → PDPT → PD → PT.

Each process has its own PML4. The upper half (kernel) entries are shared
across all processes by copying the kernel's PML4 upper entries on process
creation.

```
PML4 (512 × 8 = 4096 bytes, one per process)
 │
 ├─ [0..255]   user entries   (process-private)
 │
 └─ [256..511] kernel entries (copied from kernel_pml4 at process creation)
```

Page table entries use standard x86-64 flags:
- Bit 0: Present
- Bit 1: Writable
- Bit 2: User accessible
- Bit 63: No-execute (NX)

Huge pages (2 MiB, order-9 buddy blocks) are used for the kernel image and
identity map. User pages use 4 KiB entries for fine-grained control.

---

## 7. Kernel Heap (Slab over Buddy)

`kmalloc` / `kfree` provide variable-size kernel allocations.

### Size classes

| Class | Object size | Slab size | Objects/slab |
|-------|-------------|-----------|--------------|
| 0     | 8           | 4 KiB     | 512          |
| 1     | 16          | 4 KiB     | 256          |
| 2     | 32          | 4 KiB     | 128          |
| 3     | 64          | 4 KiB     | 64           |
| 4     | 128         | 4 KiB     | 32           |
| 5     | 256         | 4 KiB     | 16           |
| 6     | 512         | 4 KiB     | 8            |
| 7     | 1024        | 8 KiB     | 8            |
| 8     | 2048        | 8 KiB     | 4            |
| 9     | 4096        | 4 KiB     | 1            |
| large | > 4096      | buddy     | 1            |

Allocations > 4096 bytes bypass the slab and go directly to `amm_phys_alloc`.

Each slab is a page (or pages) from `amm_phys_alloc`. The first portion holds
a bitmap of free/used slots; the rest holds objects.

```
Slab layout (4 KiB, 64-byte objects):
┌─────────────────────────────┐
│ bitmap[8]  (64 bits = 64 slots) │
│ _pad to 64 bytes            │
├─────────────────────────────┤
│ obj[0]   64 bytes           │
│ obj[1]   64 bytes           │
│ ...                         │
│ obj[63]  64 bytes           │
└─────────────────────────────┘
```

---

## 8. Page Fault Handler Flow (ASCII)

```
#PF exception (vector 14)
        │
        ▼
x86_pf_handler(rip, cr2, error_code)
        │
        ├─ error_code & 0x4 (user mode)?
        │      No  → kernel bug → kernel_panic()
        │
        ├─ Get current process VA map
        │
        ├─ amm_region_find(map, cr2)
        │      NULL → no region → SIGSEGV → sched_exit(-1)
        │
        ├─ error_code & 0x2 (write) && !(region->prot & PROT_WRITE)?
        │      → SIGSEGV
        │
        ├─ amm_phys_alloc(order=0) → paddr
        │      0 → out of memory → SIGOOM (custom signal) → sched_exit(-1)
        │
        ├─ zero-fill page: memset(phys_to_virt(paddr), 0, PAGE_SIZE)
        │
        ├─ map_page(map->cr3, cr2, paddr, region->prot)
        │      walks/creates PML4→PDPT→PD→PT entries
        │
        └─ invlpg(cr2)  ← invalidate TLB entry
              │
              └─ return from exception → retry faulting instruction
```
