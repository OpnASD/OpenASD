# asduki — Unified Kernel Image Format

Version: 1.0

---

## 1. Overview

An **ASD UKI** (`asd.efi`) is a PE/COFF EFI binary that embeds the kernel,
the early driver archive, and boot metadata in well-defined PE sections. The
firmware loads it as a standard EFI application; no separate bootloader is
required for single-OS systems.

`asdboot` also supports UKI via an `entry { uki = ... }` stanza (see
`doc/asdboot-conf.md`).

---

## 2. PE Section Layout

```
asd.efi (PE32+ EFI binary)
│
├── .text        Kernel machine code (executable, read-only after map)
├── .rodata      Kernel read-only data
├── .data        Kernel writable data (BSS follows)
├── .uname       ASD version string (null-terminated ASCII, ≤ 64 bytes)
├── .cmdline     Default kernel command line (null-terminated, ≤ 4096 bytes)
├── .initrd      Compressed early driver archive (ASAR format, zstd-compressed)
└── .osrel       OS release key=value pairs (null-terminated, ≤ 4096 bytes)
```

### Section properties

| Section   | Flags            | Notes                                              |
|-----------|------------------|----------------------------------------------------|
| `.text`   | r-x              | Standard PE code section                          |
| `.rodata` | r--              | Standard PE read-only data                        |
| `.data`   | rw-              | Standard PE data section                          |
| `.uname`  | r-- (custom)     | Parsed by tools, not executed                     |
| `.cmdline`| r-- (custom)     | Overridden by `asdboot` if user edits cmdline      |
| `.initrd` | r-- (custom)     | Loaded by stub into memory before jumping to kernel|
| `.osrel`  | r-- (custom)     | Metadata for firmware/bootloaders                  |

Custom sections use `IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ`.

---

## 3. `.uname` Content

Null-terminated ASCII string, maximum 64 bytes (including null).

```
ASD 1.0.0-dev (x86_64) #1 Mon Jan  1 00:00:00 UTC 2026
```

---

## 4. `.cmdline` Content

Null-terminated ASCII string, maximum 4096 bytes. Default parameters used when
booting directly from firmware (no bootloader cmdline override).

```
root=/dev/disk/by-label/ASDROOT loglevel=2
```

---

## 5. `.osrel` Content

Null-terminated text in `KEY=value` format, one pair per line.

```
NAME=ASD
VERSION=1.0.0
ID=asd
PRETTY_NAME=ASD 1.0.0
```

---

## 6. `.initrd` Compression

The early driver archive is zstd-compressed. The stub decompresses it at boot
before handing control to the kernel.

- Compression: zstd, default level.
- The uncompressed format is ASAR (ASD Archive); see `doc/adf-manifest.md` for
  the driver module format embedded within.

---

## 7. UKI Boot Path (detailed)

```
UEFI firmware
    │
    │  loads asd.efi as EFI_IMAGE_MACHINE_X64 application
    ▼
PE stub entry point  (linked at start of .text)
    │
    ├─ 1. Locate .cmdline section → save pointer
    ├─ 2. Locate .initrd section  → decompress (zstd) into EfiRuntimeServicesData pool
    ├─ 3. Locate .uname  section  → print to EFI console
    ├─ 4. Call GetMemoryMap / ExitBootServices
    ├─ 5. Fill asd_bib_t:
    │       bib.initrd_phys = decompressed buffer
    │       bib.initrd_len  = decompressed size
    │       bib.cmdline_phys = .cmdline section address
    │       ... (rest from EFI memory map)
    ├─ 6. Disable interrupts
    └─ 7. Jump to kernel_main  (symbol at start of .text after stub)
              rdi = &bib
              rsi = ASD_BOOT_MAGIC
```

The stub is a small (~1 KiB) piece of position-independent code prepended to
`.text` by the `make uki` build step.

---

## 8. Build Process (`make uki`)

```
make uki
```

Steps performed by `kernel/Makefile`:

1. Build kernel objects → `kernel.elf` (linked at `0xFFFFFFFF80000000`).
2. Build early driver archive → `early.asar` → compress to `early.asar.zst`.
3. Extract `.text`, `.rodata`, `.data` from `kernel.elf`.
4. Assemble the UKI stub (`kernel/uki_stub.S`) into `uki_stub.o`.
5. Link stub + kernel sections + custom sections into `asd.efi` (PE32+ format)
   using `lld-link` with a linker script.
6. Inject `.uname`, `.cmdline`, `.initrd`, `.osrel` content into the EFI binary
   using `llvm-objcopy --add-section`.

### Makefile excerpt

```makefile
UKI_UNAME   := $(shell cat kernel/VERSION)
UKI_CMDLINE := root=/dev/disk/by-label/ASDROOT loglevel=2

asd.efi: kernel.elf early.asar.zst uki_stub.o
	llvm-objcopy \
	    --add-section .uname=<(echo -n "$(UKI_UNAME)") \
	    --add-section .cmdline=<(echo -n "$(UKI_CMDLINE)") \
	    --add-section .initrd=early.asar.zst \
	    --add-section .osrel=kernel/osrel.txt \
	    --set-section-flags .uname=readonly,data \
	    --set-section-flags .cmdline=readonly,data \
	    --set-section-flags .initrd=readonly,data \
	    --set-section-flags .osrel=readonly,data \
	    kernel_pe.efi $@
```

---

## 9. Verifying a UKI

The `asduki` tool (built as part of userland) can inspect a UKI binary:

```
asduki :inspect asd.efi
```

Output:

```
asduki: ASD Unified Kernel Image
  .uname   : ASD 1.0.0-dev (x86_64) #1 Mon Jan  1 00:00:00 UTC 2026
  .cmdline : root=/dev/disk/by-label/ASDROOT loglevel=2
  .initrd  : 1,048,576 bytes (compressed), 3,145,728 bytes (uncompressed)
  .osrel   : NAME=ASD VERSION=1.0.0
  .text    : 0x1000 – 0x2FFFFF  (3,141,632 bytes)
```
