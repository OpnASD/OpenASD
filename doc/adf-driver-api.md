# adf — ASD Driver Framework API

Version: 1.0  
Source: `kernel/drv/`

---

## 1. Overview

`adf` (ASD Driver Framework) defines the lifecycle, IPC contract, and binary
format for loadable kernel modules. Drivers communicate with userland
**exclusively** via Tier 0/1 IPC (shared memory / ring buffers). No ioctl.
No `/dev` node read/write as primary path.

---

## 2. Driver Lifecycle

```
dctl load :path /lib/drv/e1000.adf
    │
    ▼
asd_mod_load(path)
    │
    ├─ 1. Read .adf ELF, validate manifest (see doc/adf-manifest.md)
    ├─ 2. Allocate module VA range in kernel space
    ├─ 3. Relocate ELF sections
    ├─ 4. Call driver_entry->probe()
    │        Returns 0: device found, proceed
    │        Returns -1: device not present, unload
    ├─ 5. Call driver_entry->attach()
    │        Sets up ring buffers, shared regions
    │        Returns mod_id on success
    └─ 6. Driver is now ACTIVE; listed in module table

dctl unload :name e1000
    │
    ▼
asd_mod_unload(mod_id)
    ├─ 1. Call driver_entry->detach()
    │        Tears down ring buffers, unregisters regions
    ├─ 2. Flush pending Tier 2 messages
    ├─ 3. Free module VA range
    └─ 4. Remove from module table

System suspend → driver_entry->suspend()
System resume  → driver_entry->resume()
```

---

## 3. Driver Entry Structure

Every driver must export a symbol named `adf_entry` of type `adf_driver_t`:

```c
typedef struct {
    /* Identification */
    uint32_t    adf_magic;     /* ADF_ENTRY_MAGIC */
    uint16_t    adf_version;   /* ADF_API_VERSION */
    uint16_t    _pad;

    /* Human-readable name (matches manifest) */
    char        name[64];

    /* Lifecycle hooks — all required except suspend/resume */
    int       (*probe)  (adf_dev_t *dev);
    int       (*attach) (adf_dev_t *dev);
    void      (*detach) (adf_dev_t *dev);
    int       (*suspend)(adf_dev_t *dev);
    int       (*resume) (adf_dev_t *dev);
} adf_driver_t;

#define ADF_ENTRY_MAGIC  0xADF00001U
#define ADF_API_VERSION  1
```

---

## 4. Device Context (`adf_dev_t`)

The framework allocates an `adf_dev_t` and passes it to every lifecycle hook.
The driver stores its private state in `dev->priv`.

```c
typedef struct adf_dev {
    uint32_t   mod_id;         /* assigned by framework on load    */
    uint32_t   bus_type;       /* ADF_BUS_PCI / ADF_BUS_VIRTIO / ADF_BUS_PLATFORM */
    uint64_t   bus_addr;       /* PCI BDF or base address          */

    /* Shared memory regions created by this driver */
    vaddr_t    rx_ring_va;     /* Tier 1 ring: device → userland   */
    vaddr_t    tx_ring_va;     /* Tier 1 ring: userland → device   */
    vaddr_t    ctrl_shm_va;    /* Tier 0: control/status page      */

    /* Control port (Tier 2) */
    port_t     ctrl_port;

    /* Driver private data */
    void      *priv;

    /* Framework internal */
    adf_driver_t *entry;
    char          name[64];
    uint32_t      state;       /* ADF_STATE_* */
    uint32_t      _pad;
} adf_dev_t;

#define ADF_BUS_PCI      1
#define ADF_BUS_VIRTIO   2
#define ADF_BUS_PLATFORM 3

#define ADF_STATE_PROBING  0
#define ADF_STATE_ACTIVE   1
#define ADF_STATE_DETACHED 2
#define ADF_STATE_SUSPENDED 3
```

---

## 5. Lifecycle Hook Contracts

### `probe(dev)` → int

Called once after the module is loaded. The driver must check whether the
hardware it supports is present on the bus.

- Return 0: hardware found, proceed to `attach`.
- Return -1: hardware not found, framework unloads the module.

The driver must not allocate resources in `probe`. Use `attach` for that.

### `attach(dev)` → int

Called after a successful `probe`. The driver must:

1. Map hardware registers / DMA buffers.
2. Create ring buffer shared regions via `asd_shmap`.
3. Create a control port via `asd_port_open`.
4. Fill `dev->rx_ring_va`, `dev->tx_ring_va`, `dev->ctrl_shm_va`, `dev->ctrl_port`.
5. Enable the device and start servicing interrupts.

Return 0 on success, -1 on failure (framework calls `detach` then unloads).

### `detach(dev)`

Called when unloading. Must:
1. Disable device interrupts.
2. Unmap DMA and MMIO regions.
3. Call `asd_shunmap` on all shared regions created during `attach`.
4. Call `asd_port_close` on `dev->ctrl_port`.

### `suspend(dev)` / `resume(dev)` → int

Called on system power state transitions. Optional — return 0 to indicate
"handled, proceed", -1 to veto the transition.

---

## 6. Ring Buffer Naming Convention

Drivers create named shared regions following this pattern:

```
drv.<name>.<instance>.<direction>
```

Examples:
```
drv.e1000.0.rx     ← NIC 0 receive ring (device → userland)
drv.e1000.0.tx     ← NIC 0 transmit ring (userland → device)
drv.e1000.0.ctrl   ← NIC 0 control/status page (Tier 0)
```

Control port name:
```
drv.e1000.0.port
```

---

## 7. Interrupt Handling

Hardware interrupts are handled in kernel context (ISR). The ISR must:
1. Acknowledge the hardware interrupt (EOI / device register).
2. Copy received data into the Tier 1 ring buffer.
3. Return — **no sleeping in ISR context**.

Userland polls the ring buffer or uses a Tier 2 port notification. ADF does
not have a `select`/`poll` analogue — use a dedicated receiver thread with
`ringbuf_pop` in a tight loop or yield-loop.

---

## 8. Framework API (kernel internal)

```c
/* Load a driver module from a .adf ELF file */
mod_id_t adf_load(const char *path);

/* Unload a module by ID */
int      adf_unload(mod_id_t id);

/* Find a loaded module by name */
adf_dev_t *adf_find(const char *name);

/* Enumerate loaded modules */
void adf_foreach(void (*cb)(adf_dev_t *dev, void *arg), void *arg);

/* Called by interrupt dispatch — routes to the correct ISR */
void adf_dispatch_irq(uint32_t vector);
```
