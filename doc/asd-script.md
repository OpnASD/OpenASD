# ASD Scripting Language Specification

Version: 1.0  
Status: Authoritative

Used by: `asdrc` service definitions, `asdinstall`, `asdsh` `.asd` scripts.

---

## 1. Design Goals

- Simple enough to be parsed by a 300-line recursive descent parser.
- No external dependencies — runs before any userland tools are available.
- Service definitions (asdrc) and installer screens (asdinstall) are written in
  the same language — no separate DSL per subsystem.
- Line-oriented: one statement per line (continuation with `\` at line end).
- No semicolons required.

---

## 2. File Structure

Every `.asd` file is a sequence of **statements**.  
Blank lines and lines starting with `#` are ignored.

```
# This is a comment
#!asdsh                  ← shebang, treated as comment by parser

let x 42
echo $x
```

---

## 3. Lexical Rules

### 3.1 Tokens

| Token type   | Rule                                                    |
|--------------|---------------------------------------------------------|
| Word         | Any non-whitespace sequence not starting with a special char |
| String       | `"..."` — may contain spaces and escape sequences       |
| Option       | `:name` — colon followed by a bare word                 |
| Variable ref | `$name` or `$ENV:name`                                  |
| Capture      | `(...)` — command substitution                          |
| Number       | Decimal integer literal                                 |
| Operator     | `==` `!=` `<` `>` `<=` `>=` `=` `+` `-` `*` `/` `%` `>>` |

### 3.2 Line continuation

A line ending with `\` continues on the next line. The `\` and the newline are
both discarded; the two lines are treated as one.

```
cp :from /very/long/source/path \
   :to /very/long/destination/path
```

### 3.3 Whitespace

Tokens are separated by spaces or tabs. Indentation is ignored by the parser
(it is conventional, not syntactic, except inside `here` blocks — see §10).

---

## 4. Variables

### 4.1 let — assignment

```
let name value
let name (command)        ← capture expression
let name (calc expr)      ← arithmetic
```

Variable names: `[a-zA-Z_][a-zA-Z0-9_]*`.

### 4.2 Expansion

```
$name                     ← expand variable
$ENV:name                 ← expand environment variable
${name}suffix             ← disambiguate when followed by non-separator text
```

If a variable is undefined, it expands to the empty string.

### 4.3 Scope

Variables are **block-scoped**. A variable declared inside `if`, `loop`, `for`,
or `fn` is not visible outside the block.

The top-level scope of a script is the **script scope**. Variables in script
scope do not automatically become environment variables; use `env :set` for that.

---

## 5. Control Flow

### 5.1 if / elif / else / end

```
if <condition>
  ...
elif <condition>
  ...
else
  ...
end
```

### 5.2 Conditions

A **condition** is one of:

```
$x == "hello"
$n > 0
$n >= 1 and $n <= 10
not $flag == "yes"
(command)               ← true if exit code is 0
```

Operators: `==`, `!=`, `<`, `>`, `<=`, `>=`.  
Combinators: `and`, `or`, `not` (left-to-right, no precedence — use parentheses
to group: `($a == 1) or ($b == 2)`).

### 5.3 loop / break / next / end

```
loop
  ...
  if <condition>
    break
  end
  ...
  if <condition>
    next
  end
  ...
end
```

### 5.4 repeat / end

```
repeat <n>
  ...
end
```

`$i` is implicitly available inside a `repeat` block (0-based counter).

### 5.5 for / in / end

```
for <var> in <word-list>
  ...
end
```

The `<word-list>` is any expression that produces a whitespace-separated list of
tokens: a literal list, a variable, or a capture expression.

```
for f in /etc/asdrc/net.svc /etc/asdrc/syslog.svc
  echo "loading $f"
end

for pid in (procs :bare :field pid)
  halt :pid $pid
end
```

### 5.6 match / case / else / end

Pattern-matching control flow:

```
match $value
  case "start"
    echo "starting"
  case "stop"
    echo "stopping"
  else
    echo "unknown: $value"
end
```

Cases are exact string matches. Glob patterns are allowed with `:glob`:

```
match $filename
  case :glob "*.log"
    echo "log file"
  case :glob "*.conf"
    echo "config file"
  else
    echo "other"
end
```

---

## 6. Functions

```
fn <name> [:<param> ...]
  ...
  return <code>
end
```

- Parameters are option-style (`:param`).
- Inside the body, `$param` refers to the value passed as `:param value`.
- `return <n>` sets the exit code; bare `return` is equivalent to `return 0`.
- A function that falls off the end returns 0.

Calling a function is identical to calling a command:

```
fn add :a :b
  let result (calc $a + $b)
  echo $result
end

let sum (add :a 3 :b 4)   # sum = "7"
```

Functions are defined at parse time and can be called before their textual
position in the file (forward references are resolved in a two-pass parse).

---

## 7. Built-in Commands

These are available in all contexts (scripts, service definitions, installer).

| Command              | Description                                           |
|----------------------|-------------------------------------------------------|
| `let <var> <val>`    | Assign variable                                       |
| `echo <args...>`     | Print arguments separated by space, followed by newline |
| `return <n>`         | Exit current function or script with code n           |
| `exit <n>`           | Exit current process with code n                      |
| `break`              | Exit nearest enclosing loop                           |
| `next`               | Next iteration of nearest enclosing loop              |
| `run <path> [opts]`  | Execute a script file                                 |
| `calc <expr>`        | Integer arithmetic; prints result                     |
| `assert <cond>`      | Abort with message if condition is false              |
| `log <level> <msg>`  | Write to system log (levels: debug info warn error)   |
| `sleep <ms>`         | Sleep for milliseconds                                |
| `read :into <var>`   | Read one line from stdin into variable                |
| `env :set k v`       | Set environment variable                              |
| `env :get k`         | Print environment variable                            |
| `env :unset k`       | Unset environment variable                            |

---

## 8. Service Definition Format (asdrc)

Service files live in `/etc/asdrc/` and have the extension `.svc`.  
They are parsed by `asdinit` using the ASD script parser with additional
top-level keywords: `service` and `end`.

```
service <name>
  needs:   <dep> [<dep> ...]
  binary:  <path>
  args:    [:<opt> <val> ...]
  restart: never | always | on-fail
  log:     ring | file | none
  user:    <username>
  group:   <groupname>
  env:     <KEY>=<value> [...]
end
```

### Field descriptions

| Field     | Required | Description                                              |
|-----------|----------|----------------------------------------------------------|
| `needs`   | No       | Space-separated list of service names that must be running first |
| `binary`  | Yes      | Absolute path to the service executable                  |
| `args`    | No       | Arguments passed to the binary at launch                 |
| `restart` | No (default: `never`) | Restart policy                              |
| `log`     | No (default: `ring`) | Log output destination                        |
| `user`    | No (default: `root`) | Run service as this user                      |
| `group`   | No (default: `root`) | Run service as this group                     |
| `env`     | No       | Additional environment variables                         |

### Example

```
service syslog
  needs:   kernel
  binary:  /sbin/asdlog
  restart: on-fail
  log:     ring
end

service network
  needs:   syslog
  binary:  /sbin/netd
  restart: always
  log:     ring
  env:     IFACE=eth0
end

service sshd
  needs:   network syslog
  binary:  /sbin/sshd
  args:    :port 22
  restart: on-fail
  log:     file
  user:    sshd
  group:   sshd
end
```

The `needs` graph is resolved by `asdinit` into a DAG; services with no
unresolved dependencies are launched in parallel.

---

## 9. Capture Expressions

`(command [:option value] ...)` captures the standard output of a command and
returns it as a string (trailing newline stripped).

```
let host  (show /etc/hostname)
let count (procs :count)
let total (calc $count + 1)
```

Nested captures are supported:

```
let n (calc (procs :count) + 1)
```

---

## 10. Here Blocks

Inline multi-line text can be embedded with `here`:

```
let msg here
  Line one of the message.
  Line two of the message.
end
```

The content of a `here` block is a literal string with leading indentation
(matching the first content line) stripped. The assigned variable contains the
block as a single string with embedded newlines.

---

## 11. Error Handling

By default, a script continues after a command returns non-zero.  
Use `set :strict` at the top of a script to abort on any non-zero exit:

```
#!asdsh
set :strict

cp :from /etc/foo :to /tmp/foo
echo "done"              # only reached if cp succeeds
```

`assert` is available for explicit checks:

```
assert $? == 0
assert (procs :count) > 0
```

On failure, `assert` prints the failed condition and the script file/line, then
calls `exit 1`.

---

## 12. Import

Scripts can include other scripts with `import`:

```
import /lib/asd/stdlib.asd
import /etc/asdrc/helpers.asd
```

`import` runs the named script in the current scope. Functions defined in it
become available to the importing file. Variables from the top-level scope of
the imported file are also visible (unless shadowed).

---

## 13. Grammar Summary (EBNF)

```ebnf
file        = { statement } ;
statement   = comment
            | let-stmt
            | if-stmt
            | loop-stmt
            | repeat-stmt
            | for-stmt
            | match-stmt
            | fn-def
            | return-stmt
            | import-stmt
            | set-stmt
            | command-stmt
            ;

comment     = '#' { any-char } NEWLINE ;

let-stmt    = 'let' name expr NEWLINE ;
expr        = capture | word | string | variable ;
capture     = '(' command-stmt ')' ;

if-stmt     = 'if' condition NEWLINE { statement }
              { 'elif' condition NEWLINE { statement } }
              [ 'else' NEWLINE { statement } ]
              'end' NEWLINE ;

loop-stmt   = 'loop' NEWLINE { statement } 'end' NEWLINE ;

repeat-stmt = 'repeat' expr NEWLINE { statement } 'end' NEWLINE ;

for-stmt    = 'for' name 'in' expr NEWLINE { statement } 'end' NEWLINE ;

match-stmt  = 'match' expr NEWLINE
              { 'case' [ ':glob' ] expr NEWLINE { statement } }
              [ 'else' NEWLINE { statement } ]
              'end' NEWLINE ;

fn-def      = 'fn' name { ':' name } NEWLINE { statement } 'end' NEWLINE ;

condition   = cond-expr { ('and' | 'or') cond-expr } ;
cond-expr   = [ 'not' ] ( '(' condition ')'
                         | expr compare-op expr
                         | capture ) ;
compare-op  = '==' | '!=' | '<' | '>' | '<=' | '>=' ;

command-stmt = word { token } NEWLINE ;
token       = word | string | option | variable | capture ;
option      = ':' name [ expr ] ;
```
