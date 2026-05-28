# Stormfile Reference

A `Stormfile` is the project description file that sk reads to build your project.
It lives in the root of your project directory.

---

## Structure

A Stormfile has two sections:

- **Top-level** — global settings, variable assignments, guards, and codegen
- **Targets** — named build configurations

```
cc: /usr/bin/gcc
cflags: -std=c23 -Wall

target myapp
{
    out: myapp
    mode: release
    sources:
    src/
}
```

---

## Assignment vs append

`:` assigns a value. `::` appends to the existing value.

```
cflags: -std=c23 -Wall       # assign
cflags:: -O2                 # append — final: -std=c23 -Wall -O2
```

Only `cflags`, `lflags`, `defines`, `sources`, `includes`, and `exclude` support `::`.

---

## Top-level keywords

| Keyword | Required | Description |
|---|---|---|
| `cc` | required | Path to the C compiler — sk will error if missing |
| `compiler` | | Alias for `cc` |
| `linker` | | Linker to use (e.g. `mold`, `lld`) — optional |
| `cflags` | | Global compiler flags inherited by all targets |
| `lflags` | | Global linker flags inherited by all targets |
| `defines` | | Global preprocessor defines inherited by all targets |

**Example:**
```
cc: /usr/bin/gcc
linker: mold
cflags: -std=c23 -Wall -Werror -Wextra
lflags: -lpthread -lxxhash
```

---

## Variables

User-defined variables can be assigned at the top level and used in `if()` conditions.

```
build_type: debug
version: 2

if(build_type == debug)
{
    cflags:: -g
    defines:: -DDEBUG
}
```

Variables injected via `--set` on the CLI are also available as truthy values in conditions.

---

## Target block

Defines a named build target. Multiple targets can coexist in one Stormfile.

```
target <name>
{
    ...
}
```

### Target keywords

| Keyword | Default | Description |
|---|---|---|
| `out` | | Output binary name |
| `out_dir` | | Override output directory |
| `mode` | `debug` | Build mode: `debug` or `release` — see warning below |
| `kind` | `exec` | Output kind: `exec`, `static`, `shared` |
| `sources` | | Source files or directories to compile |
| `includes` | | Include paths |
| `cflags::` | | Append compiler flags |
| `lflags::` | | Append linker flags |
| `defines::` | | Append preprocessor defines |
| `exclude` | | Paths to exclude from directory scanner |
| `depends` | | Target dependencies — built first, libs auto-linked |
| `install` | | Install destination after a successful build |
| `print` | | Print a message or builtin value during eval |
| `exit` | | Abort eval with a log message |

> **mode defaults to `debug`** — always set `mode: release` explicitly on release targets.
> Output path is `out_dir/mode/bin/out_name`, so a missing `mode` on a release target will silently place the binary under `debug/` regardless of your flags.

---

## sources

Sources can be a directory (scanner mode) or an explicit file list (manual mode).

**Scanner mode** — sk recursively scans the directory and compiles all `.c .C .cpp .cxx .cc .s .S` files it finds:

```
sources:
src/
external/lib/src/
```

**Manual mode** — only the listed files are compiled, newlines are ignored:

```
sources:
main.c util.c
src/math.c
src/platform/linux.c
```

**Mixed** — combine both with `::`:

```
sources: src/
sources:: extra/helper.c
```

Project organization matters in scanner mode — use `exclude` to filter out subdirectories you don't want compiled.

---

## includes

Include paths use `-I` prefix and are parsed as `NODE_FLAG`:

```
includes: -Isrc
includes:: -Isrc/core -Iexternal/lib/include
```

**Quoted flags** — wrap multiple tokens in double quotes to pass compiler arguments
that require a separate path or value argument:

```
includes: -I. -Iexternal "-include src/precompiled.h"
```

The quoted string is split on whitespace at parse time and expanded into separate
arguments. This works for any flag that takes an argument (`-include`, `-isystem`,
`-imacros`, etc).

This applies to `cflags`, `lflags`, `defines`, and `includes` — anywhere a flag list is accepted.

---

## exclude

Excludes paths from the directory scanner. Takes a path:

```
exclude:
    build/
    test/
    docs/
```

Or append:

```
exclude: test/
exclude:: docs/
```

---

## depends

Declares a dependency on another target. sk resolves build order via topological sort and automatically injects `-L` and `-l` flags for linked libraries.

```
target mylib
{
    out: mylib
    kind: static
    sources:
    lib/
}

target myapp
{
    out: myapp
    depends: mylib
    sources:
    src/
}
```

---

## codegen

Generates a C header file during eval, before compilation begins.

```
codegen <output_path> {
    literal: "<string>"
    define: <KEY> <value>
}
```

- `literal` — emits a raw string line into the header
- `define` — emits a `#define KEY value` line

**Example:**
```
codegen src/config.h {
    literal: "#pragma once"
    define: VERSION_MAJOR 1
    define: VERSION_MINOR 0
    define: VERSION_STRING "1.0.0"
}
```

Generated output:
```c
#pragma once
#define VERSION_MAJOR 1
#define VERSION_MINOR 0
#define VERSION_STRING "1.0.0"
```

---

## Conditionals

sk supports `if` / `else` blocks evaluated at build time. Blocks can be used at the top level, inside targets, or wrapping entire target blocks at the top level. Nesting is supported.

```
if(<condition>)
{
    ...
}
else
{
    ...
}
```

### Operators

| Operator | Description |
|---|---|
| `==` | Equal |
| `!=` | Not equal |
| `<` | Less than |
| `<=` | Less than or equal |
| `>` | Greater than |
| `>=` | Greater than or equal |

### Value types in conditions

| Type | Example |
|---|---|
| Truthy check | `if(full_opt)` |
| Identifier | `if(build_type == debug)` |
| String | `if(__arch__ == "x86_64")` |
| Number | `if(2 <= 3)` |
| Boolean | `if(flag == true)` |

---

## print

Prints a message or builtin value to the log during eval.

```
print: "Building release target"
print: __sk_version__
```

---

## exit

Aborts eval with a log message. Can be used at the top level to guard before any target runs. 

```
if(__arch__ != "x86_64")
{
    exit: "This project requires x86_64"
}

if(__sk_version__ != "0.4.2")
{
    exit: "This project requires sk 0.4.2"
}
```

---

## CLI variable injection

Boolean variables can be injected from the command line without modifying the Stormfile:

```bash
sk --set=full_opt strike
sk --set=asan --set=full_opt strike
```

Any variable passed via `--set` evaluates as truthy in `if()` blocks.
Injecting a name that conflicts with a builtin will warn and be ignored.

---

## Builtin variables

Set automatically by sk at eval time. Available in `if()` conditions and `print` statements.

### Version

| Variable | Description |
|---|---|
| `__sk_version__` | Full sk version string (e.g. `0.4.2`) |
| `__sk_version_major__` | Major version number |
| `__sk_version_minor__` | Minor version number |
| `__sk_version_patch__` | Patch version number |

### Environment

| Variable | Description |
|---|---|
| `__os__` | Operating system (e.g. `linux`) |
| `__arch__` | CPU architecture (e.g. `x86_64`) |
| `__cache_line__` | CPU cache line size in bytes |

### Compiler

| Variable | Description |
|---|---|
| `__gcc__` | Set if compiling with GCC |
| `__clang__` | Set if compiling with Clang |

### CPU features

| Variable | Description |
|---|---|
| `__has_avx__` | CPU supports AVX |
| `__has_avx2__` | CPU supports AVX2 |
| `__has_sse4_2__` | CPU supports SSE 4.2 |
| `__has_bmi__` | CPU supports BMI |

### Git

| Variable | Description |
|---|---|
| `__git_hash__` | Current git commit hash |
| `__git_branch__` | Current git branch |

---

## Full example

```
cc: /usr/bin/gcc
linker: mold
cflags: -std=c23 -Wall -Werror -Wextra -pedantic
lflags: -lpthread
major: 1
minor: 0
patch: 0

if(__arch__ != "x86_64")
{
    exit: "This project requires x86_64"
}

codegen src/config.h {
    literal: "#pragma once"
    define: VERSION_MAJOR major
    define: VERSION_MINOR minor
    define: VERSION_PATCH patch
    define: VERSION_STRING "major.minor.patch"
}

target myapp
{
    out: myapp
    mode: release
    kind: exec
    print: __sk_version__
    if(__clang__)
    {
        cflags:: -Wdocumentation
    }
    if(build_type == debug)
    {
        cflags:: -g
        defines:: -DDEBUG
    }
    else
    {
        cflags:: -O2
        defines:: -DNDEBUG
    }
    if(full_opt)
    {
        cflags:: -O3 -flto=auto
        lflags:: -flto=auto -Wl,-O1 -Wl,--as-needed
    }
    sources:
    src/
    includes:
    -Isrc
    exclude:
    test/
    docs/
    // install: ~/.local/bin
}
```

---

## Limits

| Resource | Limit |
|---|---|
| Targets | 256 |
| Dependencies per target | 32 |
| Excludes per target | 32 |
| Includes per target | 256 |
| Defines per target | 512 |
| Flags per target | 256 |
| Libs | 128 |
| Variables | 8192 |

> These are compile-time constants. If your project approaches these limits, consider splitting into multiple targets with `depends`.
