# asdsh Syntax Specification

Version: 1.0  
Status: Authoritative

---

## 1. Overview

`asdsh` is the ASD interactive shell and command interpreter. It is not POSIX sh,
not Bash, not any existing shell. It shares no compatibility goal with them.

Key design decisions:
- Options are always **named**, never single-letter flags.
- The option prefix is `:` (colon), followed by the option name, followed by the value.
- Boolean options (switches) use `:option` with no value.
- No positional argument ambiguity — every argument has a role.
- Data flow uses **funnels** (`>>`) instead of pipes. See §5.
- Quoting uses `"double quotes"` only. No single-quote strings.
- Comments start with `#`.

---

## 2. Command Syntax

### 2.1 Basic form

```
command [:option [value]] ...
```

A command is a bare word. Options follow in any order.  
If an option takes a value, the value is the next token.  
If an option is boolean (a switch), it takes no value.

### 2.2 Examples

```
# List a directory
ls /usr/bin

# List with detail view (boolean switch)
ls /usr/bin :long

# List sorted by size
ls /usr/bin :sort size

# Copy a file
cp :from /etc/hostname :to /tmp/hostname.bak

# Search for a pattern recursively
seek :pat "error" :in /var/log :recurse

# Mount a disk
mnt :dev /dev/disk0s1 :at /mnt/usb :type asdfs

# Terminate a process
halt :pid 1042

# Terminate by name
halt :name httpd
```

### 2.3 Bare positional arguments

Some commands accept a single bare positional argument as a shorthand for their
primary target. The command spec defines whether this is allowed.

```
# Equivalent:
ls /usr/bin
ls :dir /usr/bin
```

When both a positional and the named option are given, the named option wins.

---

## 3. Quoting and Escaping

- Tokens containing spaces or special characters must be quoted with `"..."`.
- Inside a double-quoted string, `\"` is a literal quote, `\\` is a literal backslash.
- `\n`, `\t`, `\r` are escape sequences inside double-quoted strings.
- No other escape sequences are recognised.

```
seek :pat "out of memory" :in /var/log
echo "path is: /usr/bin"
cp :from "my file.txt" :to /tmp/safe.txt
```

---

## 4. Variables

Variables are set with `let` and expanded with `$name`.

```
let base /usr/local
ls $base/bin
cp :from $base/etc/hosts :to /tmp/hosts
```

Variable names are alphanumeric plus `_`, case-sensitive, must start with a letter.

Environment variables are a separate namespace, accessed via the `env` command:

```
env :set CC clang
env :get CC          # prints value
env :unset TMPDIR
```

Within a script, `$ENV:name` expands an environment variable:

```
let compiler $ENV:CC
```

---

## 5. Funnels — Data Flow

Instead of Unix pipes, asdsh uses **funnels** (`>>`).  
A funnel connects the **output stream** of the left command to the **input stream**
of the right command.

```
command-a >> command-b >> command-c
```

This is semantically equivalent to a pipeline, but uses a distinct operator to
signal that ASD funnels are not Unix pipes (they are implemented over Tier 1 IPC
ring buffers, not file descriptors).

### 5.1 Redirect to file

```
command >> :file /tmp/output.txt          # write output to file (overwrite)
command >> :file /tmp/output.txt :append  # append to file
```

### 5.2 Redirect from file

```
:file /tmp/input.txt >> command           # feed file into command input
```

### 5.3 Mixed

```
:file data.txt >> seek :pat "fail" >> :file results.txt
```

---

## 6. Control Flow (interactive and script)

Control flow constructs are valid both interactively and in `.asd` script files.

### 6.1 if / elif / else / end

```
if $code == 0
  echo "success"
elif $code == 1
  echo "soft error"
else
  echo "fatal"
end
```

Comparison operators: `==`, `!=`, `<`, `>`, `<=`, `>=`.  
String comparisons use the same operators.  
Boolean: `and`, `or`, `not`.

```
if $size > 0 and $ready == "yes"
  echo "go"
end
```

### 6.2 loop / end

```
loop
  let line (readline)
  if $line == ""
    break
  end
  echo $line
end
```

### 6.3 repeat / end

Fixed iteration count:

```
repeat 5
  echo "tick"
end
```

### 6.4 for / in / end

Iterate over whitespace-separated words:

```
for f in (ls /etc :bare)
  show $f
end
```

`(command)` is a **capture expression** — runs a command and substitutes its output
as a word list. See §7.

### 6.5 break / next

`break` exits the nearest enclosing `loop`, `repeat`, or `for`.  
`next` continues to the next iteration.

---

## 7. Capture Expressions

`(command [:option value] ...)` runs a command and substitutes its standard output
as a token (or word list in `for ... in` context).

```
let hostname (cat :file /etc/hostname)
echo "running on $hostname"

for pid in (procs :bare :field pid)
  echo "pid $pid"
end
```

---

## 8. Functions

Functions are defined with `fn` and called like commands.

```
fn greet :name
  echo "Hello, $name"
end

greet :name "world"
```

Parameters are declared as option names after `fn name`. Inside the body they are
available as `$name`.

Return value: functions set `$?` to their exit code (0 = success).

```
fn divide :a :b
  if $b == 0
    echo "division by zero"
    return 1
  end
  let result ($a / $b)
  echo $result
  return 0
end

let val (divide :a 10 :b 2)
```

---

## 9. Arithmetic

Arithmetic is performed with `calc`:

```
let x (calc 3 + 4)
let y (calc $x * 2)
let z (calc $y / 3)
```

Operators: `+`, `-`, `*`, `/`, `%`.  
Integer arithmetic only in the base language. Floating-point is not provided by
the shell; use a dedicated utility.

---

## 10. Special Variables

| Variable | Meaning                                     |
|----------|---------------------------------------------|
| `$?`     | Exit code of the last command               |
| `$self`  | Name of the running script file             |
| `$args`  | All arguments passed to a script, as a list |
| `$pid`   | PID of the current asdsh process            |

---

## 11. Script Files

Script files use the extension `.asd`. The first line is a shebang-style marker:

```
#!asdsh
```

Scripts are invoked by name:

```
/etc/asdrc/startup.asd
./myscript.asd
```

Or via the `run` built-in:

```
run /etc/asdrc/startup.asd :arg1 foo :arg2 bar
```

---

## 12. Exit Codes

| Code | Meaning                        |
|------|--------------------------------|
| 0    | Success                        |
| 1    | General error                  |
| 2    | Usage / bad arguments          |
| 3    | Resource not found             |
| 4    | Permission denied              |
| 127  | Command not found              |

---

## 13. Completion and Line Editing

Interactive features (not relevant to scripts):

- `Tab` completes commands, paths, and `:option` names.
- `Up`/`Down` navigate history.
- `Ctrl+C` sends interrupt to the foreground command.
- `Ctrl+D` on an empty line exits the shell.
- History is saved to `/var/asdsh/history`.

---

## 14. Prompt

The default prompt is:

```
[user@host dir]>
```

It is configured via the `PROMPT` environment variable using the following
escape sequences:

| Sequence | Expands to              |
|----------|-------------------------|
| `%u`     | Current username        |
| `%h`     | Hostname (short)        |
| `%d`     | Current directory       |
| `%?`     | Last exit code          |
| `%n`     | Newline                 |

Example:

```
env :set PROMPT "[%u@%h %d]> "
```
