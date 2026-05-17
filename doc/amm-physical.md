# amm — Physical Memory Allocator Design

Version: 1.0

---

## 1. Chosen Design: Buddy Allocator

**Decision**: Buddy system over slab for the physical allocator.

### Tradeoff analysis

| Criterion          | Buddy system                          | Slab                                 |
|--------------------|---------------------------------------|--------------------------------------|
| Fragmentation      | Internal frag up to 50%; external frag eliminated | Low for fixed-size objects; bad for variable sizes |
| Latency            | O(log N) alloc/free                   | O(1) for cached slabs                |
| Complexity         | Moderate (~300 lines)                 | Higher (requires zone management)    |
| Large allocations  | Excellent (coalescing to huge pages)  | Poor — not designed for it           |
| Kernel use case    | DMA buffers, page tables, driver memory | Per-object kernel caches           |
| Code size          | Small                                 | Larger                               |

**Verdict**: The buddy allocator handles the full range of sizes the kernel
needs (single pages for PCBs, contiguous ranges for DMA, 2 MiB blocks for huge
pages). A slab layer is added **on top** of the buddy allocator for kernel heap
objects (`kmalloc`/`kfree`), giving the best of both worlds.

---

## 2. Buddy System Overview

```
order 0: 4 KiB   (1 page)
order 1: 8 KiB   (2 pages)
order 2: 16 KiB  (4 pages)
...
order 9: 2 MiB   (512 pages)  ← huge page size
order 10: 4 MiB  (1024 pages)
...
order MAX_ORDER: largest contiguous block

MAX_ORDER = 18  (1 GiB blocks)
```

### Free list structure

One free list per order. Each list is a singly-linked list of free blocks.
Each block's first 8 bytes hold the `next` pointer while free (repurposed as
data when allocated).

```
free_lists[0] → [block 0x1000] → [block 0x2000] → NULL
free_lists[1] → [block 0x4000] → NULL
free_lists[2] → NULL
...
```

### Buddy calculation

The buddy of a block at physical address `P` with order `O` is:

```
buddy(P, O) = P XOR (1 << (PAGE_SHIFT + O))
```

---

## 3. Memory Map Initialisation

```
amm_phys_init(mmap, count, entry_sz)
    │
    ├─ For each ASD_MEM_FREE entry:
    │     ├─ Align base up to PAGE_SIZE
    │     ├─ Align end down to PAGE_SIZE
    │     └─ Call amm_phys_free_range(base, pages)
    │           └─ Repeatedly find highest order that fits,
    │              call amm_phys_free(addr, order)
    │
    └─ Total free page count stored in g_phys.free_pages
```

---

## 4. Allocation Algorithm

```
amm_phys_alloc(order):
    for o = order to MAX_ORDER:
        if free_lists[o] is not empty:
            remove block B from free_lists[o]
            while o > order:
                o--
                split B into B and buddy(B, o)
                add buddy(B, o) to free_lists[o]
            mark B as allocated
            return B.paddr
    return 0  // out of memory
```

---

## 5. Deallocation Algorithm

```
amm_phys_free(paddr, order):
    b = paddr
    while order < MAX_ORDER:
        buddy = b XOR (1 << (PAGE_SHIFT + order))
        if buddy is in free_lists[order]:
            remove buddy from free_lists[order]
            b = min(b, buddy)
            order++
        else:
            break
    add b to free_lists[order]
    mark b as free
```

---

## 6. Page Metadata Array

A flat array of `page_info_t` structs, indexed by page frame number (PFN):

```
page_info_t (8 bytes per page):
┌──────────────────────────────────────┐
│ flags     uint16   PAGE_FREE, PAGE_ALLOC, PAGE_RESERVED │
│ order     uint8    buddy order (valid when PAGE_FREE)    │
│ refcount  uint8    reference count (shared mappings)     │
│ map_count uint16   number of VA maps referencing page    │
│ _reserved uint16                                         │
└──────────────────────────────────────┘
```

The array lives at a fixed kernel virtual address, computed during init:

```
PAGE_INFO_BASE = 0xFFFF880000000000
page_info(pfn) = PAGE_INFO_BASE + pfn * sizeof(page_info_t)
```

For a 64 GiB physical address space, the metadata array is 128 MiB — acceptable.

---

## 7. ASCII: Physical Memory Layout After Init

```
Physical address space (not to scale):

0x0000_0000  ┌──────────────────┐
             │  Reserved / BIOS │  ASD_MEM_RESERVED
0x0001_0000  ├──────────────────┤
             │  Low memory      │  ASD_MEM_FREE (some)
0x0010_0000  ├──────────────────┤
             │  Kernel image    │  ASD_MEM_KERNEL (mapped at 0xFFFF...80000000)
             │  page_info array │
             │  early stack     │
~0x0200_0000 ├──────────────────┤
             │  Free RAM        │  ASD_MEM_FREE → buddy free lists
             │  (managed by amm)│
             │                  │
             │                  │
   top RAM   └──────────────────┘
```
