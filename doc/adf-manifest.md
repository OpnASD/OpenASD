# adf — Driver Module Manifest Format

Version: 1.0

---

## 1. Overview

Every `.adf` driver module embeds a binary manifest in a dedicated ELF section
named `.adfmeta`. The manifest describes the driver's identity, hardware
compatibility, required capabilities, and version information.

The kernel reads the manifest before executing any driver code, allowing it to
reject incompatible or unsafe modules.

---

## 2. ELF Structure of a `.adf` File

```
driver.adf  (ELF64 relocatable object)
├── .text        driver code
├── .rodata      driver read-only data
├── .data        driver writable data
├── .bss         driver zero-init data
├── .adfmeta     binary manifest (see §3)
└── .symtab      symbol table (must export 'adf_entry')
```

The `.adf` file is a **relocatable ELF** (type `ET_REL`), not a shared object.
The framework resolves relocations against kernel symbols at load time using a
small fixed export table (see §5).

---

## 3. Manifest Binary Format

All fields are little-endian. Total fixed header: 256 bytes.

```c
#define ADF_META_MAGIC   0x4144464D45544131ULL  /* "ADFMETA1" */
#define ADF_META_VERSION 1
#define ADF_MAX_COMPAT   8    /* max hardware compatibility entries */
#define ADF_NAME_LEN     64

typedef struct {
    uint64_t magic;           /*   0: ADF_META_MAGIC                      */
    uint16_t version;         /*   8: ADF_META_VERSION                    */
    uint16_t api_version;     /*  10: ADF_API_VERSION the driver targets  */
    uint32_t flags;           /*  12: ADF_FLAG_* bitmask                  */
    char     name[ADF_NAME_LEN]; /* 16: human-readable driver name         */
    char     author[64];      /*  80: author string                       */
    uint32_t driver_version;  /* 144: BCD: 0x0100 = 1.0, 0x0203 = 2.3   */
    uint32_t compat_count;    /* 148: number of compat entries (≤ 8)      */
    uint32_t rx_slot_size;    /* 152: Tier 1 RX ring slot size            */
    uint32_t rx_slot_count;   /* 156: Tier 1 RX ring slot count           */
    uint32_t tx_slot_size;    /* 160: Tier 1 TX ring slot size            */
    uint32_t tx_slot_count;   /* 164: Tier 1 TX ring slot count           */
    uint32_t ctrl_shm_size;   /* 168: Tier 0 control page size            */
    uint8_t  _reserved[84];   /* 172: zero-padded                         */
    /* 256: compat[] entries follow (compat_count × sizeof(adf_compat_t)) */
} adf_meta_t;
```

### Flags

| Bit | Name                | Meaning                                    |
|-----|---------------------|--------------------------------------------|
| 0   | `ADF_FLAG_PCI`      | Driver binds to PCI devices                |
| 1   | `ADF_FLAG_VIRTIO`   | Driver binds to VirtIO devices             |
| 2   | `ADF_FLAG_PLATFORM` | Driver binds to platform (hardcoded) devs  |
| 3   | `ADF_FLAG_NETWORK`  | Driver exposes a network interface         |
| 4   | `ADF_FLAG_STORAGE`  | Driver exposes a storage interface         |
| 5   | `ADF_FLAG_INPUT`    | Driver exposes an input interface          |
| 6   | `ADF_FLAG_GRAPHICS` | Driver exposes a framebuffer               |

---

## 4. Hardware Compatibility Entry (`adf_compat_t`)

```c
typedef struct {
    uint32_t bus_type;    /* ADF_BUS_PCI / ADF_BUS_VIRTIO / ADF_BUS_PLATFORM */
    uint32_t vendor_id;   /* PCI vendor ID (0 = wildcard)                     */
    uint32_t device_id;   /* PCI device ID (0 = wildcard)                     */
    uint32_t class_code;  /* PCI class code (0 = wildcard)                    */
    uint32_t subsystem_vendor;
    uint32_t subsystem_device;
    uint64_t _reserved;
} adf_compat_t;  /* 32 bytes */
```

---

## 5. Kernel Symbol Export Table

The framework provides a fixed set of kernel symbols for driver relocation.
Drivers may only call functions from this list. Calling any other kernel
address results in a load-time error.

```
asd_shmap              amm_phys_alloc         kmalloc
asd_shunmap            amm_phys_free          kfree
asd_port_open          sched_current          ringbuf_init
asd_port_close         sched_wake             ringbuf_push
asd_port_send          asd_time_ns            ringbuf_pop
asd_port_recv          lapic_eoi
```

---

## 6. Example: Minimal Driver

```c
/* SPDX-License-Identifier: BSD-2-Clause */
/* e1000.c — Minimal Intel e1000 driver stub */

#include <adf/adf.h>

#define E1000_VENDOR   0x8086
#define E1000_DEVICE   0x100E

static int e1000_probe(adf_dev_t *dev) {
    if (dev->bus_type != ADF_BUS_PCI) return -1;
    /* PCI probe: check BDF config space */
    /* (real probe reads PCI config via MMIO) */
    return 0;  /* found */
}

static int e1000_attach(adf_dev_t *dev) {
    /* Create RX ring (device → userland) */
    dev->rx_ring_va = asd_shmap("drv.e1000.0.rx",
        ringbuf_required_size(2048, 256), SHM_CREATE);
    ringbuf_init((void *)dev->rx_ring_va, 2048, 256);

    /* Create TX ring (userland → device) */
    dev->tx_ring_va = asd_shmap("drv.e1000.0.tx",
        ringbuf_required_size(2048, 256), SHM_CREATE);
    ringbuf_init((void *)dev->tx_ring_va, 2048, 256);

    /* Create control/status page */
    dev->ctrl_shm_va = asd_shmap("drv.e1000.0.ctrl",
        4096, SHM_CREATE);

    /* Create control port */
    dev->ctrl_port = asd_port_open("drv.e1000.0.port", PORT_CREATE);

    /* TODO: initialise NIC hardware registers */
    return 0;
}

static void e1000_detach(adf_dev_t *dev) {
    asd_shunmap(dev->rx_ring_va);
    asd_shunmap(dev->tx_ring_va);
    asd_shunmap(dev->ctrl_shm_va);
    asd_port_close(dev->ctrl_port);
}

adf_driver_t adf_entry = {
    .adf_magic   = ADF_ENTRY_MAGIC,
    .adf_version = ADF_API_VERSION,
    .name        = "e1000",
    .probe       = e1000_probe,
    .attach      = e1000_attach,
    .detach      = e1000_detach,
    .suspend     = NULL,
    .resume      = NULL,
};
```

Manifest is generated at build time by `mkmanifest`:

```
mkmanifest \
  :name e1000 \
  :author "ASD Project" \
  :version 1.0 \
  :pci 0x8086:0x100E \
  :flag network \
  :rx-slot-size 2048 :rx-slot-count 256 \
  :tx-slot-size 2048 :tx-slot-count 256 \
  :ctrl-shm-size 4096 \
  :out e1000.adfmeta

llvm-objcopy --add-section .adfmeta=e1000.adfmeta e1000.o
```
