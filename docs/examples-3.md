# Stormfile Examples — Part 3

---

## Part 7 — Real World: Chocolate Doom

This is what a full production Stormfile looks like. Chocolate Doom is a
faithful source port of the original Doom engine, supporting four games
(Doom, Heretic, Hexen, Strife) plus tooling binaries.

A few patterns from earlier examples come together here:

- Top-level `cc`, `cflags`, `lflags`, `defines` inherited by all targets
- `lflags::` per target appends only what that target actually needs
- `codegen` generates `src/config.h` before any compilation starts
- `kind: static` builds foundational libraries compiled once and reused
- `depends` wires the build order and injects `-L` and `-l` automatically
- `exclude` surgically removes subdirectories from a directory scan

### Project tree (abridged)

```
chocolate-doom/
├── Stormfile
├── src/
│   ├── doom/
│   ├── heretic/
│   ├── hexen/
│   ├── strife/
│   └── setup/
├── textscreen/
├── opl/
└── pcsound/
```

### Stormfile

See [`Stormfile-chocolate-doom`](Stormfile-chocolate-doom) for the full build definition.

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

```
// Stormfile for Chocolate Doom
cc: /usr/bin/gcc
linker: mold

cflags:
    -std=c99 -Wall -Wextra -Wdeclaration-after-statement -Wredundant-decls
    -Isrc -I/usr/include/SDL2

lflags: -lSDL2 -lm

defines: -D_GNU_SOURCE -D_DEFAULT_SOURCE

cflags:: -O2 -march=native

major: 3
minor: 1
patch: 1

codegen src/config.h {
    literal: "#pragma once"

    define: PACKAGE_NAME        "Chocolate Doom"
    define: PACKAGE_SHORTNAME   "Chocolate"
    define: PACKAGE_TARNAME     "chocolate-doom"
    define: PACKAGE_VERSION     "major.minor.patch"
    define: PACKAGE_STRING      "Chocolate Doom major.minor.patch"
    define: PACKAGE_BUGREPORT   "chocolate-doom-dev-list@chocolate-doom.org"
    define: PACKAGE_COPYRIGHT   "Copyright (C) 1993-2025"
    define: PACKAGE_LICENSE     "GNU General Public License, version 2"

    define: PROGRAM_PREFIX      "chocolate-"
    define: PROGRAM_SPREFIX     "chocolate"

    define: HAVE_DECL_STRCASECMP  1
    define: HAVE_DECL_STRNCASECMP 1
    define: HAVE_DIRENT_H         1

    define: HAVE_LIBSAMPLERATE  1
    define: HAVE_LIBPNG         1
    define: HAVE_FLUIDSYNTH     1
}

// ----------------------------------------------------
// Core Foundational Libraries
// ----------------------------------------------------

target textscreen
{
    out:  textscreen
    kind: static
    sources: textscreen/
    includes:
        -Itextscreen
}

target opl
{
    out:  opl
    kind: static
    sources: opl/
    includes:
        -Iopl
}

target pcsound
{
    out:  pcsound
    kind: static
    sources: pcsound/
    includes:
        -Ipcsound
}

// ----------------------------------------------------
// Game Engine Submodules (Static Frameworks)
// ----------------------------------------------------

target lib_doom
{
    out:  doom
    kind: static
    sources: src/doom/
    includes:
        -Isrc/doom
        -Itextscreen
}

target lib_heretic
{
    out:  heretic
    kind: static
    sources: src/heretic/
    includes:
        -Isrc/heretic
        -Itextscreen
}

target lib_hexen
{
    out:  hexen
    kind: static
    sources: src/hexen/
    includes:
        -Isrc/hexen
        -Itextscreen
}

target lib_strife
{
    out:  strife
    kind: static
    sources: src/strife/
    includes:
        -Isrc/strife
        -Itextscreen
}

target lib_setup
{
    out:  setup
    kind: static
    sources: src/setup/
    includes:
        -Isrc/setup
        -Itextscreen
}

// ----------------------------------------------------
// Primary Binaries
// ----------------------------------------------------

target chocolate_doom
{
    out:     chocolate-doom
    mode:    release
    depends: textscreen opl pcsound lib_doom
    lflags:: -lSDL2_mixer -lSDL2_net -lsamplerate -lpng -lfluidsynth
    includes:
        -Isrc/doom
        -Itextscreen
        -Iopl
        -Ipcsound
    sources: src/
    exclude:
        src/doom/
        src/heretic/
        src/hexen/
        src/strife/
        src/setup/
        src/d_dedicated.c
        src/z_zone.c
}

target chocolate_heretic
{
    out:     chocolate-heretic
    mode:    release
    depends: textscreen opl pcsound lib_heretic
    lflags:: -lSDL2_mixer -lSDL2_net -lsamplerate -lpng -lfluidsynth
    includes:
        -Isrc/heretic
        -Itextscreen
        -Iopl
        -Ipcsound
    sources: src/
    exclude:
        src/doom/
        src/heretic/
        src/hexen/
        src/strife/
        src/setup/
        src/d_dedicated.c
        src/z_zone.c
}

target chocolate_hexen
{
    out:     chocolate-hexen
    mode:    release
    depends: textscreen opl pcsound lib_hexen
    lflags:: -lSDL2_mixer -lSDL2_net -lsamplerate -lpng -lfluidsynth
    includes:
        -Isrc/hexen
        -Itextscreen
        -Iopl
        -Ipcsound
    sources:
        src/
        src/deh_str.c
    exclude:
        src/doom/
        src/heretic/
        src/hexen/
        src/strife/
        src/setup/
        src/d_dedicated.c
        src/z_zone.c
        src/deh_main.c
        src/deh_text.c
}

target chocolate_strife
{
    out:     chocolate-strife
    mode:    release
    depends: textscreen opl pcsound lib_strife
    lflags:: -lSDL2_mixer -lSDL2_net -lsamplerate -lpng -lfluidsynth
    includes:
        -Isrc/strife
        -Itextscreen
        -Iopl
        -Ipcsound
    sources: src/
    exclude:
        src/doom/
        src/heretic/
        src/hexen/
        src/strife/
        src/setup/
        src/d_dedicated.c
        src/z_zone.c
}

target chocolate_server
{
    out:     chocolate-server
    mode:    release
    lflags:: -lSDL2_net
    sources:
        src/i_main.c
        src/i_system.c
        src/m_argv.c
        src/m_misc.c
        src/d_dedicated.c
        src/d_iwad.c
        src/d_mode.c
        src/deh_str.c
        src/i_timer.c
        src/m_config.c
        src/net_common.c
        src/net_dedicated.c
        src/net_io.c
        src/net_packet.c
        src/net_sdl.c
        src/net_query.c
        src/net_server.c
        src/net_structrw.c
        src/z_native.c
}

target chocolate_setup
{
    out:     chocolate-setup
    mode:    release
    depends: textscreen lib_setup
    lflags:: -lSDL2_mixer -lSDL2_net
    includes:
        -Isrc/setup
        -Itextscreen
    sources:
        src/i_main.c
        src/i_system.c
        src/m_argv.c
        src/m_misc.c
        src/deh_str.c
        src/d_mode.c
        src/d_iwad.c
        src/i_timer.c
        src/m_config.c
        src/m_controls.c
        src/net_io.c
        src/net_packet.c
        src/net_petname.c
        src/net_sdl.c
        src/net_query.c
        src/net_structrw.c
        src/z_native.c
}

target midiread
{
    out:     midiread
    mode:    release
    cflags:: -DTEST
    sources:
        src/midifile.c
        src/z_native.c
        src/i_system.c
        src/m_argv.c
        src/m_misc.c
        src/d_iwad.c
        src/deh_str.c
        src/m_config.c
}

target mus2mid
{
    out:     mus2mid
    mode:    release
    cflags:: -DSTANDALONE
    sources:
        src/mus2mid.c
        src/memio.c
        src/z_native.c
        src/i_system.c
        src/m_argv.c
        src/m_misc.c
        src/d_iwad.c
        src/deh_str.c
        src/m_config.c
}
```

=======
>>>>>>> dev
>>>>>>> local
### Build

```
$ sk strike
```

16 targets, 4 game engines, built by gcc with mold in one shot. To see the
full compiler and linker invocations as they happen:

```
$ sk strike --verbose
```

To profile the full build from scratch:

```
$ sk strike --profile
```

```
====== Profiler ======
Lexer  : 93.45 us
Parser : 1.01 ms
Eval   : 10.11 ms
Compile: 1.30 min
Link   : 1.332 s
Strike : 1.32 min
======================
Total  : 1.32 min
======================
```

After the build, check the status of all 16 targets at once:

```
$ sk status
```

```
Version: 0.5.1
Working directory: /devenv/repos/chocolate-doom
Cache size: 33.88 MB
=============================== STORM-KNELL STATUS ============================================
  Target Name         Kind           Status Check        Total Files       Age
  --------------------------------------------------------------------------------------
  ✔ textscreen          STATIC         [OPERATIONAL]       23             2 min ago
  ✔ opl                 STATIC         [OPERATIONAL]       10             2 min ago
  ✔ pcsound             STATIC         [OPERATIONAL]       5              2 min ago
  ✔ lib_doom            STATIC         [OPERATIONAL]       59             2 min ago
  ✔ lib_heretic         STATIC         [OPERATIONAL]       47             2 min ago
  ✔ lib_hexen           STATIC         [OPERATIONAL]       48             2 min ago
  ✔ lib_strife          STATIC         [OPERATIONAL]       59             2 min ago
  ✔ lib_setup           STATIC         [OPERATIONAL]       15             2 min ago
  ✔ chocolate_doom      EXEC           [OPERATIONAL]       66             2 min ago
  ✔ chocolate_heretic   EXEC           [OPERATIONAL]       66             2 min ago
  ✔ chocolate_hexen     EXEC           [OPERATIONAL]       64             2 min ago
  ✔ chocolate_strife    EXEC           [OPERATIONAL]       66             2 min ago
  ✔ chocolate_server    EXEC           [OPERATIONAL]       19             2 min ago
  ✔ chocolate_setup     EXEC           [OPERATIONAL]       17             2 min ago
  ✔ midiread            EXEC           [OPERATIONAL]       8              2 min ago
  ✔ mus2mid             EXEC           [OPERATIONAL]       9              2 min ago
  --------------------------------------------------------------------------------------
  Cache Summary:  285 hits, 296 misses (Total Ops: 581)
  Workspace Status: READY / HEALTHY (All targets verified up-to-date).
===============================================================================================
```

To run a target, grab its name from the status table and pass it to `surge`:

```
$ sk surge chocolate_doom
```

`surge` runs the binary directly. For WAD files and runtime requirements
refer to the [Chocolate Doom repository](https://github.com/chocolate-doom/chocolate-doom).

---
