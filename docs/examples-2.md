# Stormfile Examples — Part 2

---

## Part 4 — Static Libraries and Dependencies

As a project grows, splitting code into static libraries keeps things
organized and speeds up incremental builds — only what changed gets recompiled.

### Project tree

```
project/
├── Stormfile
├── main.c
└── src/
    ├── math/
    │   ├── math.c
    │   └── math.h
    └── util/
        ├── util.c
        └── util.h
```

### Stormfile

```
cc: /usr/bin/clang
cflags: -Wall -Wextra -Werror

target libmath
{
    out:  math
    kind: static
    sources:
        src/math/
    includes:
        -Isrc/math
}

target libutil
{
    out:  util
    kind: static
    sources:
        src/util/
    includes:
        -Isrc/util
}

target app
{
    out:     app
    mode:    release
    depends: libmath libutil  // sk resolves build order and injects -L and -l automatically
    cflags:: -O2
    sources: main.c
    includes:
        -I.
        -Isrc/math
        -Isrc/util
}
```

`depends` does two things: it guarantees `libmath` and `libutil` are compiled
before `app`, and it auto-injects the correct `-L<path>` and `-l<name>` flags
into the linker invocation for `app`. No manual `lflags:: -lmath -lutil` needed.

Build order is resolved via topological sort, so no matter how deep your
dependency graph gets, sk always compiles in the right order.

### Output tree

```
crater/
├── libmath/
│   └── debug/
│       ├── lib/
│       │   └── libmath.a
│       └── obj/
│           └── math.c.o
├── libutil/
│   └── debug/
│       ├── lib/
│       │   └── libutil.a
│       └── obj/
│           └── util.c.o
└── app/
    └── release/
        ├── bin/
        │   └── app
        └── obj/
            └── main.c.o
```

### Build

```
$ sk strike

$ sk strike --verbose  // shows exact compiler and linker invocations
```

`--verbose` is handy here to confirm that `-L` and `-l` were injected
correctly for each dependency.


---

## Part 6 — Conditionals, Built-ins, and Exit Guards

Stormfile has a set of built-in variables resolved at eval time — before any
compilation starts. They reflect the machine, the environment, and sk itself.

### Built-in variables

| Variable                | Description                                  |
|-------------------------|----------------------------------------------|
| `__sk_version__`        | Full sk version string e.g. `"0.5.1"`        |
| `__sk_version_major__`  | Major version number                         |
| `__sk_version_minor__`  | Minor version number                         |
| `__sk_version_patch__`  | Patch version number                         |
| `__os__`                | Operating system e.g. `"linux"`              |
| `__arch__`              | CPU architecture e.g. `"x86_64"`, `"arm64"`  |
| `__git_branch__`        | Current git branch                           |
| `__git_hash__`          | Current git commit hash                      |
| `__has_avx__`           | `"1"` if CPU supports AVX, else `"0"`        |
| `__has_avx2__`          | `"1"` if CPU supports AVX2, else `"0"`       |
| `__has_sse4_2__`        | `"1"` if CPU supports SSE4.2, else `"0"`     |
| `__has_bmi__`           | `"1"` if CPU supports BMI, else `"0"`        |
| `__cache_line__`        | CPU cache line size in bytes                 |

Built-ins are read-only. Attempting to assign to them is an error:

```
__sk_version__: 100  // [error]: Cannot assign to built-in variable '__sk_version__'
```

`--set` is equally blocked — it cannot override built-ins or any user-defined
top-level variable, only inject new boolean flags.

### Operators

Conditions support `==`, `!=`, `>`, `<`, `>=`, `<=`.
Conditions cannot be combined with `&&` or `||` — use nested or sequential
`if` blocks instead.

### Stormfile

```
cc: /usr/bin/gcc
cflags: -Wall -Wextra -Werror -std=c23

major: 0
minor: 1
patch: 0
app_name: "app"

codegen include/build_info.h
{
    literal: "#pragma once"
    define: APP_NAME          app_name
    define: APP_VERSION_MAJOR major
    define: APP_VERSION_MINOR minor
    define: APP_VERSION_PATCH patch
    define: APP_VERSION_STRING "major.minor.patch"
    define: GIT_BRANCH        __git_branch__
    define: GIT_HASH          __git_hash__
}

// Hard exit if sk is too old
if(__sk_version_minor__ < 5)
{
    exit: "This project requires sk 0.5.x or newer"
}

// Linux-specific flags
if(__os__ == "linux")
{
    print: "Linux detected"
    cflags:: -pthread
    lflags:: -lpthread
}

// CPU feature detection
if(__has_avx2__)
{
    cflags:: -mavx2
}

if(__has_bmi__)
{
    cflags:: -mbmi
}

target app
{
    out:  app
    mode: release
    cflags:: -O2 -march=native
    defines:: -DNDEBUG -DZERO=0

    if(fullopt)
    {
        cflags:: -O3 -flto=auto
        lflags:: -flto=auto -Wl,-O1 -Wl,--as-needed
    }

    sources: src/
    includes:
        -Isrc
        -Iinclude

    if(install)
    {
        install: ~/.local/bin
    }
}
```

`exit:` halts evaluation immediately with an error message — nothing compiles.
It's the right tool for enforcing version requirements before sk wastes any time.

`print:` writes to stdout during eval. It takes one thing at a time — a string
literal, a built-in, or a user variable:

```
print: "Linux detected"   // literal string
print: __sk_version__     // built-in  →  [sk]: __sk_version__ = 0.5.1
print: app_name           // user var  →  [sk]: app_name = app
```

### Build

```
$ sk strike

$ sk strike --set=fullopt --set=install  // optimized build, installed on completion

$ sk strike --verbose                    // see exact flags resolved per target
```

---
