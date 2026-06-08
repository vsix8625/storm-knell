# Storm-Knell (sk)
A fast, cache-aware build tool with its own DSL — describe your project once, let sk figure out the rest.

---

## Requirements

- GCC or Clang (C23)
- [xxhash](https://github.com/Cyan4973/xxHash)
- pthreads *(usually ships with your system)*
- [mold](https://github.com/rui314/mold) *(recommended, but not required)*

Before running the bootstrap script, ensure the following are installed:

- **General:** `git`, `libxxhash` (Development Headers)
- **Arch Linux:** `sudo pacman -S base-devel xxhash`

---

## Installation

### From source (recommended)

```bash
git clone --recurse-submodules https://github.com/vsix8625/storm-knell.git
cd storm-knell
python3 ./scripts/build.py
```

The bootstrap script compiles sk using your system compiler. Once built, sk rebuilds itself using its own Stormfile and installs to `~/.local/bin`.

---

## Platform Support

| Platform | Status |
| :--- | :--- |
| **Linux x86_64** | Verified (Arch, Ubuntu, Fedora) |

---

## Quick Start

Initialize sk in your project:

```bash
sk init
```

`sk init` creates a starter `Stormfile` and discovers available compilers on your system. Edit the Stormfile to match your project, then build:

```bash
sk strike
```

Commands and flags can be mixed freely. The only positional rule is that `-C <path>` must be followed immediately by its directory. `init` should come before `strike` when setting up a new project.

```bash
sk -C myproject init strike --profile surge --main-c
```

---

## In practice

sk builds itself. Clean build with warm object cache — 41 source files, 345ms total:

```
$ sk clean strike --profile
[log]: Cleaning workspace targets...
[log]:   Wiping target [sk] outputs -> crater/
[log]: [cache]: 41 hits, 0, 41 total
====== Profiler ======
Lexer  : 97.79 us
Parser : 5.36 ms
Eval   : 4.22 ms
Compile: 60.52 ms
Link   : 227.18 ms
Strike : 345.81 ms
======================
Total: 353.25 ms
======================
```

```
$ sk status
Storm-Knell Version: 0.5.3
================================= STORM-KNELL STATUS ==================================
  Target Name   Kind    Status Check     Total Files   Size          Age
  --------------------------------------------------------------------------------------
  ✔ sk          EXEC    [OPERATIONAL]    41            184.82 KB     just now
  --------------------------------------------------------------------------------------
  Cache Summary:  41 hits, 0 misses (Total Ops: 41)
  Workspace Status: READY / HEALTHY (All targets verified up-to-date).
=======================================================================================
```

---

## Basic Stormfile

```
cc: /usr/bin/gcc
linker: mold
cflags: -std=c23 -Wall -Werror
lflags: -lpthread

target myapp
{
    out:  myapp
    mode: release
    cflags:: -O2
    sources:
        src/
    includes:
        -Isrc
    install: ~/.local/bin
}
```

---

## Commands

| Command | Description |
|---|---|
| `sk strike` | Parse Stormfile and build project |
| `sk surge` | Run target |
| `sk clean` | Clean build artifacts |
| `sk init` | Initialize sk in working directory |
| `sk purge` | De-initialize sk from working directory |
| `sk cache` | View global cache size or nuke |
| `sk status` | View project status |
| `sk config` | Manage sk configuration |

---

## Global Options

| Option | Description |
|---|---|
| `-C <path>` | Run from path |
| `-j <N>` | Allow N parallel jobs |
| `--verbose` | Increase verbosity |
| `--silent` | No output |
| `--profile` | Show pipeline timing |
| `--set=<var>` | Inject a boolean variable into eval |
| `--force` | Force action (only for init: v0.5.3) |
| `--version` | Show version and exit |
| `--help, -h` | Show help and exit |

---

## Compiler Configuration

sk auto-discovers `gcc`, `g++`, `clang`, and `clang++` on first `sk init`. To register additional compilers:

```bash
sk config --add-cc=/usr/bin/aarch64-linux-gnu-gcc
```

Registered compilers are stored in `~/.config/storm_knell/compilers.conf`. Re-run `sk init` to pick up changes.

---

## CLI Variable Injection

Pass boolean variables into your Stormfile at build time without modifying it:

```bash
sk strike --set=lto
sk strike --set=asan --set=debug_tracy
```

In your Stormfile:

```
if(lto)
{
    cflags:: -O3 -flto=auto
    lflags:: -flto=auto -Wl,-O1 -Wl,--as-needed
}
```

---

## Passing Arguments to Surged Targets

Use `:::` to separate sk arguments from arguments passed to the target binary:

```bash
sk strike surge ::: --my-app-flag --verbose
sk clean strike surge ::: -h
```

Everything after `:::` is forwarded to the binary that `surge` runs.

---

## Caching

sk maintains a global object cache keyed on source content, compiler version, sk version, cflags, includes, and defines. Unchanged translation units are never recompiled.

```bash
sk cache          # view cache size
sk cache --nuke   # clear cache
```

---

## Notes

- `sk init --force` resets `.storm/` only — your `Stormfile` is never touched
- `sk purge` is the full reset — removes both `.storm/` and the `Stormfile`

Use `sk -h <command>` for detailed help on any command:

    sk -h clean
    sk -h strike
    sk -h surge

---

## Editor Support (Neovim)

To get syntax highlighting, automatic file detection, and auto-formatting on save for Stormfiles, copy or link these files into your Neovim configuration:

1. `utils/nvim/ftdetect/storm.lua` -> `~/.config/nvim/ftdetect/storm.lua`
2. `utils/nvim/ftplugin/storm.lua` -> `~/.config/nvim/ftplugin/storm.lua`
3. `utils/nvim/syntax/storm.lua`   -> `~/.config/nvim/syntax/storm.lua`

*(Alternatively, append the repository's `utils/nvim` directory to your Neovim `runtimepath`.)*

### LSP Setup (clangd)
To generate a `compile_commands.json` for `clangd` or other language servers, pass the `--gen-cmds` flag during a strike:

```bash
sk strike --gen-cmds
```

---

## License

[MIT](LICENSE)
