# Stormfile Examples — Part 1

A collection of examples walking through the Stormfile DSL from simplest to
most complete. Each example builds on the last.

---

## Part 1 — Single File, Single Target

The smallest valid Stormfile: one source file, one binary.

### Project tree

```
hello/
├── Stormfile
└── main.c
```

### Stormfile

```
cc: /usr/bin/gcc

target hello
{
    out:     hello
    sources: main.c
}
```

A `target` block is the core building unit of any Stormfile. `out` names the
resulting binary and `sources` lists what to compile. That's all sk needs.

### Build

```
$ sk strike
```

---

## Part 2a — Two Targets, Core Options

Before diving in, a note on `::` vs `:`.

- `cflags: ...`  — sets the value, overriding anything declared at the top level
- `cflags:: ...` — appends to whatever was declared at the top level

This applies to `cflags`, `lflags`, `defines`, and similar list fields.

### Project tree

```
project/
├── Stormfile
├── main.c
└── src/
    └── foo.c
```

### Stormfile

```
cc: /usr/bin/clang
cflags: -Wall -Wextra -Werror
lflags:
defines: -DDEBUG

target foo_app
{
    out:     foo_app  // output binary name       — default: <target_name>
    out_dir: foo      // output directory name    — default: crater
    mode:    beta     // build mode               — default: debug
    cflags:: -O2      // appends to top-level cflags
    defines: -DNDEBUG // overrides top-level defines
    sources: main.c   // exact file path
    includes: -I.     // include paths
    install: ~/.local/bin // copies binary to $HOME/.local/bin on every strike
}
```

The output binary lands at `./<out_dir>/<out>/<mode>/bin/<out>`.
For `foo_app` that's `./foo/foo_app/beta/bin/foo_app`.

If you want install to be opt-in rather than automatic, guard it with a flag:

```
    if(install)
    {
        install: ~/.local/bin
    }
```

Then invoke it explicitly:

```
$ sk strike --set=install
```

Without `--set=install`, the binary is built but not copied anywhere.

### Build

```
$ sk strike
```

---

## Part 2b — Directory Scanning, Shared Library, Conditional Flags

### Stormfile (continuing from Part 2a)

```
target bar
{
    out:     bar
    out_dir: bar_lib
    mode:    release
    kind:    shared              // produces a shared library (.so / .dylib)
    cflags:: -O2 -march=x86-64-v2
    defines: -DNDEBUG -DBUILD_TYPE="release" // quoted string passed straight to the compiler

    if(fullopt)                  // injected at build time via: sk strike --set=fullopt
    {
        cflags:
            -Wall
            -Wextra
            -Werror
            -O3
            -march=x86-64-v2
            -flto
        lflags:: -flto
    }

    sources:
        src/                     // scans src/ and all subdirs recursively
    exclude:
        src/test/                // skip the test directory
        src/storm.cpp            // skip a specific file
    includes:
        -I.
        -Isrc
}
```

`sources:` with a directory path tells sk to scan recursively for any file
matching: `.c  .cc  .cxx  .cpp  .CC  .s  .S`

Quoted string values in `defines` are passed straight through to the compiler:

```
defines: -DBUILD_TYPE="release"
```

produces:

```
... -DBUILD_TYPE="release" ...
```

This only works inside `cflags` or `defines` — not as a top-level variable assignment.

To toggle `fullopt` at build time:

```
$ sk strike --set=fullopt
```

Multiple flags can be set at once:

```
$ sk strike --set=install --set=fullopt
```

Without a flag, its `if()` block is simply ignored.

> **A note on assembly files:** `.s` and `.S` files found during a directory
> scan are passed straight to the compiler as-is, right alongside your C files.
> If your `src/` tree contains assembly you *don't* want compiled, use `exclude`
> to skip it explicitly.

---

## Part 3 — Generating a Version Header with `codegen`

Hardcoding version strings across your source tree is a pain.
`codegen` solves this by generating a header at build time from values
defined in the Stormfile itself.

### Project tree

```
project/
├── Stormfile
└── src/
    └── main.c
```

### Stormfile

```
cc: /usr/bin/clang
cflags: -Wall -Wextra -Werror

major: 1
minor: 0
patch: 0
foo:   "foo"

codegen include/version.h
{
    literal: "#pragma once"

    define: FOO_STR            foo               // expands to: #define FOO_STR "foo"
    define: APP_VERSION_MAJOR  major
    define: APP_VERSION_MINOR  minor
    define: APP_VERSION_PATCH  patch
    define: APP_VERSION_STRING "major.minor.patch"
    define: APP_COMPILED_GCC   __gcc__
    define: APP_COMPILED_CLANG __clang__
}

target foo
{
    out:     foo
    sources: src/
    includes:
        -Isrc
        -Iinclude
}
```

`codegen` runs before compilation. By the time sk touches your `.c` files,
`include/version.h` already exists and is ready to include.

A few things worth noting:

- `major`, `minor`, `patch`, `foo` are variables defined at the top level and
  referenced freely inside the `codegen` block
- When referencing a string variable like `foo`, write it bare — `define: FOO_STR foo`.
  Writing `define: FOO_STR "foo"` would produce `#define FOO_STR ""foo""` which
  is not what you want
- `"major.minor.patch"` in a string context expands to the actual values,
  e.g. `"1.0.0"`
- `__gcc__` and `__clang__` are built-in sk tokens that resolve to `1` or `0`
  depending on which compiler is active

### Build

```
$ sk strike
```

`include/version.h` is written first, then `foo` is compiled.

---
