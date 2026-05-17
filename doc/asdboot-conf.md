# asdboot Configuration Format

Version: 1.0  
File location: `/boot/asdboot.conf`

---

## 1. Design Rationale

The format is called **ASD Block Config** (`.abc`).  
It is not INI, not TOML, not shell, not JSON.

Design goals:
- Human-readable and human-writable.
- No sigils except `=` for assignment.
- Blocks delimit named sections with `begin`/`end`.
- No multi-document support — one file, one config.
- Comments with `#`.
- Parser must fit in ~150 lines of C with no dependencies.

---

## 2. Grammar (EBNF)

```ebnf
config      = { item } EOF ;
item        = comment | block | assignment ;
comment     = '#' { any-char } NEWLINE ;
block       = name NEWLINE { item } 'end' NEWLINE ;
assignment  = name '=' value NEWLINE ;
name        = letter { letter | digit | '_' | '-' } ;
value       = bare-word | quoted-string ;
bare-word   = non-whitespace { non-whitespace } ;
quoted-string = '"' { char | escape } '"' ;
escape      = '\' ( '"' | '\' | 'n' | 't' ) ;
```

Key rules:
- Block headers are just a `name` on its own line (no keyword, no braces).
- Blocks are closed by the bare word `end` on its own line.
- Assignments are `name = value` with exactly one `=`.
- Blank lines are ignored.
- Indentation is conventional, not syntactic.

---

## 3. Example `/boot/asdboot.conf`

```
# asdboot configuration
# loaded by asdboot at firmware handoff

timeout = 5

menu
  title = ASD Boot Menu
  default = asd-main
end

entry
  id      = asd-main
  label   = ASD (default)
  kernel  = /boot/asdkernel
  initrd  = /boot/early.img
  cmdline = root=/dev/disk/by-label/ASDROOT loglevel=2
end

entry
  id      = asd-debug
  label   = ASD (debug mode)
  kernel  = /boot/asdkernel
  initrd  = /boot/early.img
  cmdline = root=/dev/disk/by-label/ASDROOT loglevel=5 debug
end

entry
  id    = asd-uki
  label = ASD UKI
  uki   = /boot/asd.efi
end
```

---

## 4. Top-Level Keys

| Key       | Type    | Default | Description                                  |
|-----------|---------|---------|----------------------------------------------|
| `timeout` | integer | `3`     | Seconds to wait before booting default entry |

---

## 5. `menu` Block Keys

| Key       | Type   | Default       | Description                        |
|-----------|--------|---------------|------------------------------------|
| `title`   | string | `"ASD Boot"`  | Title displayed in the menu box    |
| `default` | string | first entry   | `id` of the default entry          |

---

## 6. `entry` Block Keys

| Key       | Type   | Required | Description                                              |
|-----------|--------|----------|----------------------------------------------------------|
| `id`      | string | Yes      | Unique identifier used in `default` references           |
| `label`   | string | Yes      | Human-readable label shown in menu                       |
| `kernel`  | path   | No*      | Path to the ASD kernel binary                            |
| `initrd`  | path   | No       | Path to early driver archive (optional with kernel)      |
| `cmdline` | string | No       | Kernel command-line parameters                           |
| `uki`     | path   | No*      | Path to a UKI `.efi` binary (mutually exclusive with kernel) |

\* Either `kernel` or `uki` must be present, not both.

---

## 7. Menu Rendering

When two or more entries exist (or when the user presses a key during timeout),
asdboot renders a box-drawing menu:

```
┌──────────────────────────────────────┐
│           ASD Boot Menu              │
├──────────────────────────────────────┤
│  > ASD (default)                     │
│    ASD (debug mode)                  │
│    ASD UKI                           │
├──────────────────────────────────────┤
│  [↑↓] select   [Enter] boot          │
│  [e] edit cmdline   [Esc] firmware   │
└──────────────────────────────────────┘
│  Booting in 4 seconds...             │
└──────────────────────────────────────┘
```

The selected entry is marked with `>` and highlighted (reverse video or bold,
depending on the terminal/framebuffer).

If only one entry exists and `timeout = 0`, the menu is skipped entirely.

---

## 8. Parser Error Handling

On parse error, asdboot prints:

```
asdboot: config error at line N: <message>
```

and drops to a minimal fallback prompt that allows the user to type a kernel path
manually.
