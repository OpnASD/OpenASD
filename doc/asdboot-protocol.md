# asdboot → Kernel Handoff Protocol

Version: 1.0

---

## 1. Overview

When asdboot has selected a boot entry and loaded the necessary images, it
constructs a **Boot Information Block (BIB)** in memory and transfers control to
the kernel entry point. This document defines the BIB layout, the register state
at entry, and the memory map contract.

---

## 2. Platform Assumptions

- x86-64, 64-bit long mode.
- Execution starts via UEFI (`EFI_BOOT_SERVICES.ExitBootServices` has been called
  before jumping to the kernel).
- If booting via UKI, the firmware itself satisfies §5 and jumps to the PE entry
  point; the register/BIB contract is the same.

---

## 3. Register State at Kernel Entry

| Register | Value                                                        |
|----------|--------------------------------------------------------------|
| `rdi`    | Physical address of the Boot Information Block (BIB)         |
| `rsi`    | Magic number `0xA5D'B007` (identifies ASD boot protocol v1)  |
| `rsp`    | 16-byte-aligned temporary stack (at least 64 KiB valid)      |
| `rip`    | Kernel entry point (first instruction)                       |
| `rflags` | Interrupts disabled (`IF=0`), direction flag clear           |
| All other | Undefined                                                   |

The kernel must not rely on any other register values.

---

## 4. Boot Information Block (BIB)

The BIB is a contiguous structure allocated by asdboot in conventional memory
(below 4 GiB, identity-mapped by the bootloader). Its physical address is passed
in `rdi`.

```c
/* All fields are little-endian. Sizes in bytes. */

#define ASD_BIB_MAGIC   0xA5DB0071UL   /* "ASD BIB v1" */
#define ASD_BIB_VERSION 1

typedef struct {
    uint32_t magic;          /* ASD_BIB_MAGIC                         */
    uint16_t version;        /* ASD_BIB_VERSION                       */
    uint16_t size;           /* sizeof(asd_bib_t), for forward compat */

    /* Memory map */
    uint64_t mmap_phys;      /* Physical address of asd_mmap_entry_t[] */
    uint32_t mmap_count;     /* Number of entries                      */
    uint32_t mmap_entry_sz;  /* sizeof(asd_mmap_entry_t)               */

    /* Framebuffer (set to 0 if unavailable) */
    uint64_t fb_phys;        /* Physical address of framebuffer        */
    uint32_t fb_width;       /* Width in pixels                        */
    uint32_t fb_height;      /* Height in pixels                       */
    uint32_t fb_stride;      /* Bytes per scanline                     */
    uint8_t  fb_format;      /* ASD_FB_* constant                      */
    uint8_t  fb_reserved[3];

    /* ACPI */
    uint64_t acpi_rsdp_phys; /* Physical address of ACPI RSDP, 0 if none */

    /* Kernel command line */
    uint64_t cmdline_phys;   /* Physical address of null-terminated string */
    uint32_t cmdline_len;    /* Length in bytes (excluding null)           */
    uint32_t _pad0;

    /* Initial ramdisk (early driver archive) — 0 if not loaded */
    uint64_t initrd_phys;    /* Physical address                       */
    uint64_t initrd_len;     /* Length in bytes                        */

    /* Boot timestamp */
    uint64_t boot_ns;        /* Monotonic nanoseconds at ExitBootServices */
} asd_bib_t;
```

### 4.1 Framebuffer Format Constants

| Constant         | Value | Pixel layout          |
|------------------|-------|-----------------------|
| `ASD_FB_NONE`    | 0     | No framebuffer        |
| `ASD_FB_RGBX32`  | 1     | R G B x (32 bpp)      |
| `ASD_FB_BGRX32`  | 2     | B G R x (32 bpp)      |

### 4.2 Memory Map Entry

```c
#define ASD_MEM_FREE      0   /* Usable RAM                    */
#define ASD_MEM_RESERVED  1   /* Reserved, do not use          */
#define ASD_MEM_ACPI_REC  2   /* ACPI reclaimable              */
#define ASD_MEM_ACPI_NVS  3   /* ACPI NVS, must preserve       */
#define ASD_MEM_LOADER    4   /* Used by asdboot (reclaimable after init) */
#define ASD_MEM_KERNEL    5   /* Kernel image pages            */
#define ASD_MEM_INITRD    6   /* Initial ramdisk pages         */
#define ASD_MEM_FB        7   /* Framebuffer pages             */

typedef struct {
    uint64_t base;    /* Physical base address (page-aligned) */
    uint64_t pages;   /* Number of 4 KiB pages                */
    uint32_t type;    /* ASD_MEM_* constant                   */
    uint32_t _pad;
} asd_mmap_entry_t;
```

---

## 5. Identity Mapping Contract

Before jumping to the kernel:
- The lower 4 GiB of physical memory is identity-mapped (`vaddr == paddr`).
- The kernel image is additionally mapped at its link address (typically
  `0xFFFFFFFF80000000` — the kernel defines this; asdboot honours it).
- All mappings use 2 MiB huge pages where possible.
- No UEFI mappings survive; `ExitBootServices` has been called.

The kernel is responsible for constructing its own permanent page tables during
early init. It may use the identity map until then.

---

## 6. UKI Boot Path

When the firmware loads `asd.efi` (a UKI image) directly:
1. The PE entry point is the kernel's stub decompressor.
2. The stub locates the `.initrd` PE section, decompresses it into a BIB-aligned
   buffer, and fills in `bib.initrd_phys` / `bib.initrd_len`.
3. The stub calls `ExitBootServices`, fills the BIB with UEFI memory map data,
   and jumps to the kernel proper with the same register contract as §3.

From the kernel's perspective, UKI and classic two-file boot are identical after
the handoff.

---

## 7. Version Compatibility

The `version` field allows future BIB extensions. A kernel must:
1. Check `magic == ASD_BIB_MAGIC`; panic if mismatch.
2. Check `version >= 1`; panic if lower.
3. Use `size` to avoid reading fields beyond what the bootloader wrote
   (forward compatibility: newer kernel, older bootloader).

---

## 8. Fallback Prompt

If asdboot cannot load the kernel (missing file, bad checksum), it prints:

```
asdboot: failed to load entry "asd-main": file not found
asdboot: enter kernel path (or 'halt'):
> _
```

The user may type an absolute path. asdboot retries the load with that path and
the same `cmdline` from the last attempted entry.
