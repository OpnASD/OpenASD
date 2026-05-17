# asdhed — Scheduler Internals

Version: 1.0  
Source: `kernel/sched/`

---

## 1. Overview

`asdhed` is a preemptive, tickless, priority-based scheduler. One run queue
exists per CPU. Work-stealing moves processes between queues when imbalance
exceeds a threshold.

Key properties:
- **Tickless**: no periodic timer interrupt for scheduling. The next preemption
  deadline is programmed into the LAPIC one-shot timer after each context switch.
- **Three scheduling classes**: Realtime (`SCHED_RT`), Interactive (`SCHED_INTR`),
  Batch (`SCHED_BATCH`). Classes are always prioritised in that order.
- **Shared-memory state export**: the run queue summary page is mapped read-only
  into userland so tools like `procs` need no syscall to sample process counts
  and states.
- **No fork/exec/waitpid**: the process lifecycle is entirely ASD-native.

---

## 2. Scheduling Classes

### 2.1 Realtime (`SCHED_RT`)

- Priority range: 0–99 (0 = highest).
- Round-robin within the same priority level.
- A Realtime process is never preempted by Interactive or Batch processes.
- Time quantum: none — runs until it yields, blocks, or a higher-priority RT
  process becomes runnable.
- Use cases: interrupt threads, audio drivers, deadline-sensitive I/O.

### 2.2 Interactive (`SCHED_INTR`)

- Priority range: 0–127 (dynamic, adjusted by scheduler).
- Base priority set by `asd_sched_set`; scheduler boosts priority on I/O wakeup
  and decays it during extended CPU usage (exponential decay, half-life 100 ms).
- Time quantum: 4 ms default, shortened when many interactive processes compete.
- Use cases: shells, GUI processes, interactive daemons.

### 2.3 Batch (`SCHED_BATCH`)

- Priority range: 0–63 (lower number = higher priority within class).
- Runs only when no Realtime or Interactive process is runnable on the CPU.
- Time quantum: 50 ms default.
- Use cases: compilers, archivers, background indexing.

---

## 3. Data Structures

### 3.1 Process Control Block (`pcb_t`)

```
pcb_t
┌─────────────────────────────────────────────────────────┐
│ pid          uint32   process identifier                │
│ state        uint8    PROC_RUN / PROC_READY / PROC_WAIT │
│              uint8    / PROC_DEAD / PROC_NEW            │
│ sched_class  uint8    SCHED_RT / SCHED_INTR / SCHED_BATCH│
│ priority     int16    current effective priority        │
│ base_pri     int16    base priority (set by user)       │
│ cpu          uint8    current/last CPU affinity         │
│ _pad         uint8[1]                                   │
│ cpu_ns       uint64   total CPU nanoseconds consumed    │
│ wake_ns      uint64   monotonic ns of last I/O wakeup   │
│ deadline_ns  uint64   next preemption deadline (abs ns) │
│ va_map       vmap_t * virtual address map               │
│ port_list    port_t * head of owned message ports       │
│ exit_code    int32    set on PROC_DEAD                  │
│ name[32]     char[]   human-readable process name       │
│ rq_node      rb_node  red-black tree node in run queue  │
│ reap_list    list     linked into reap list when dead   │
└─────────────────────────────────────────────────────────┘
total: ~128 bytes, cache-line aligned
```

### 3.2 Run Queue (`runq_t`)

One per CPU, cache-line padded, no sharing between CPUs during normal operation.

```
runq_t
┌─────────────────────────────────────────────────────────┐
│ lock         spinlock  protects the entire struct       │
│ cpu_id       uint8                                      │
│ _pad         uint8[7]                                   │
│                                                         │
│ rt_tree      rb_root   RT processes keyed by priority   │
│ intr_tree    rb_root   INTR processes keyed by eff_pri  │
│ batch_tree   rb_root   BATCH processes keyed by priority│
│                                                         │
│ rt_count     uint32    # RT processes                   │
│ intr_count   uint32    # INTR processes                 │
│ batch_count  uint32    # BATCH processes                │
│                                                         │
│ current      pcb_t *   currently running process        │
│ idle_pcb     pcb_t *   per-CPU idle process (PID 0)     │
│                                                         │
│ steal_ns     uint64    monotonic ns of last steal       │
│ total_ns     uint64    cumulative run time this CPU     │
└─────────────────────────────────────────────────────────┘
```

### 3.3 Global Process Table

```
proc_table
┌──────────────────────────────────────────────────────────┐
│ lock         rwlock    readers: table scan, writer: add  │
│ entries      pcb_t *[MAX_PROCS]  indexed by pid          │
│ next_pid     uint32    next PID to assign (wraps at max) │
│ count        uint32    live process count                │
└──────────────────────────────────────────────────────────┘
MAX_PROCS = 65536
```

### 3.4 ASCII Layout: run queue selection

```
  CPU 0 runq_t                CPU 1 runq_t
  ┌──────────────┐            ┌──────────────┐
  │ rt_tree      │            │ rt_tree      │
  │  [pri=0] P1  │            │  (empty)     │
  │  [pri=1] P2  │            │              │
  ├──────────────┤            ├──────────────┤
  │ intr_tree    │            │ intr_tree    │
  │  [eff=12] P3 │            │  [eff=8] P5  │
  │  [eff=40] P4 │            │  [eff=20] P6 │
  ├──────────────┤            ├──────────────┤
  │ batch_tree   │            │ batch_tree   │
  │  [pri=0] P7  │            │  (empty)     │
  └──────────────┘            └──────────────┘
         │                           │
         └──────── steal ────────────┘
           when |cpu0.count - cpu1.count| > STEAL_THRESHOLD(2)
```

---

## 4. Context Switch

```
context switch path
─────────────────────────────────────────────────────────────
  preemption interrupt fires (LAPIC)
      │
      ▼
  save_context(current)        push all GPRs + rflags to PCB.regs
      │
      ▼
  runq_dequeue_next()          pick highest-priority ready PCB
      │                        RT > INTR > BATCH
      ▼
  update_deadline(next)        program LAPIC one-shot:
      │                          RT: no timer (runs until blocks)
      │                          INTR: 4 ms from now
      │                          BATCH: 50 ms from now
      ▼
  restore_context(next)        pop GPRs + rflags from PCB.regs
      │
      ▼
  iret / sysret
```

Context storage in `pcb_t.regs`:

```
regs[0]  rax        regs[8]   r8
regs[1]  rbx        regs[9]   r9
regs[2]  rcx        regs[10]  r10
regs[3]  rdx        regs[11]  r11
regs[4]  rsi        regs[12]  r12
regs[5]  rdi        regs[13]  r13
regs[6]  rbp        regs[14]  r14
regs[7]  rsp        regs[15]  r15
regs[16] rip
regs[17] rflags
regs[18] cr3        (page table root — swapped on address space change)
regs[19] fs_base    (for TLS)
```

---

## 5. Work Stealing

Stealing is triggered after a CPU's run queue becomes empty and no process wakes
up within `STEAL_IDLE_NS` (500 µs):

1. Scan other CPUs' `runq.intr_count + runq.batch_count` (RT is not stolen).
2. Pick the CPU with the highest combined count.
3. If that count exceeds the local count by `STEAL_THRESHOLD` (2), acquire both
   run queue locks (always in CPU-ID order to avoid deadlock).
4. Move `count/2` processes from victim to local queue (lowest-priority first).
5. Release locks, re-program LAPIC.

---

## 6. Shared Memory State Export

After `amm` initialises, `asdhed` creates a named shared region:

```c
vaddr_t sched_state_page = asd_shmap("kernel.sched.state",
                                     PAGE_SIZE, SHM_RDONLY_USERLAND);
```

The page contains a read-only snapshot updated on every context switch:

```c
typedef struct {
    uint32_t magic;           /* 0x5348454DU ("SHED") */
    uint32_t cpu_count;
    struct {
        uint32_t rt_count;
        uint32_t intr_count;
        uint32_t batch_count;
        uint32_t current_pid;
        uint64_t idle_ns;     /* ns spent idle this CPU */
        uint64_t busy_ns;     /* ns spent running this CPU */
    } cpus[MAX_CPUS];         /* MAX_CPUS = 64 */
    uint32_t total_procs;
    uint32_t total_threads;
} sched_state_t;
```

Userland maps this region with `asd_shmap("kernel.sched.state", ...)` and
reads it without any syscall. The kernel writes it atomically (per-CPU word
stores are naturally atomic on x86-64; total_procs uses a seqlock).

---

## 7. Syscall API

See `doc/asd-glossary.md` for full signatures. Scheduler-relevant syscalls:

### `asd_spawn(spawn_args_t *args) → pid_t`

```c
typedef struct {
    const char *binary;       /* path to executable */
    const char **argv;        /* null-terminated argument list */
    const char **envp;        /* null-terminated environment list */
    sched_class_t sched_class;
    int           priority;
    uint8_t       cpu_affinity; /* 0xFF = no affinity */
} spawn_args_t;
```

Creates a new process with a fresh VA map. Does not copy the calling process's
address space. The new process starts at the ELF entry point.

### `asd_reap(pid_t pid, exit_info_t *out) → int`

Blocks until the target process exits, then fills `exit_info_t`:

```c
typedef struct {
    int32_t  exit_code;
    uint64_t cpu_ns;
    uint64_t wall_ns;
} exit_info_t;
```

Returns 0 on success, `-1` if pid is invalid or already reaped.

### `asd_sched_set(pid_t pid, sched_class_t cls, int priority) → int`

Changes the scheduling class and base priority of a process. Caller must own
the process (same UID) or hold kernel privilege.

### `asd_yield()`

Voluntarily surrender remaining quantum. The process stays READY and is
re-queued at the tail of its priority level.

### `asd_sleep(uint64_t ns)`

Block for at least `ns` nanoseconds. Implemented via LAPIC timer + wait queue.
Does not consume CPU time while sleeping.

---

## 8. Process States

```
        asd_spawn()
            │
            ▼
         PROC_NEW ──── init complete ───► PROC_READY
                                               │
                                    ┌──────────┴──────────┐
                               scheduled              not scheduled
                                    │
                                    ▼
                               PROC_RUN ◄──── wakeup ──── PROC_WAIT
                                    │                          ▲
                              I/O / port_recv                  │
                                    └──────────────────────────┘
                                    │
                              asd_exit()
                                    │
                                    ▼
                               PROC_DEAD ──── asd_reap() ──► (freed)
```

---

## 9. Priority Boosting (SCHED_INTR)

When an INTR process wakes from a blocking I/O call or `asd_port_recv`:

```
new_priority = max(base_priority - BOOST, 0)
```

Where `BOOST = 20` (configurable at kernel compile time).

The boosted priority decays back toward `base_priority` on each preemption:

```
effective_priority += (base_priority - effective_priority) * DECAY_FACTOR
```

Where `DECAY_FACTOR = 0.1` (integer approximation: `+= delta / 10`).

This ensures that a process doing short bursts of interactive work stays near
the top of the queue, while a process that starts burning CPU naturally drifts
toward its base priority.
