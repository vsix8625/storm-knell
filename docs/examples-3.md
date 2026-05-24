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
