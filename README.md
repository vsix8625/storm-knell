# Storm-Knell (sk)

A fast, cache-aware build tool with its own DSL — describe your project once, let sk figure out the rest.

---

## Requirements

- GCC or Clang (C23)
- [xxhash](https://github.com/Cyan4973/xxHash)
- [mold](https://github.com/rui314/mold) *(recommended, but not required)*

Before running the bootstrap script, ensure the following are installed:

- **General:** `git`, `libxxhash` (development headers)
- **Arch Linux:** `sudo pacman -S base-devel xxhash`

---

## Installation

Clone the repository:

```bash
git clone --recurse-submodules https://github.com/vsix8625/storm-knell.git
cd storm-knell
```

### Arch Linux Package

Storm-Knell can be built and installed as a native Arch Linux package using `makepkg`.

```bash
cd storm-knell-pkg/arch
makepkg -si
```

This downloads the source archive from GitHub Releases, verifies its SHA-256 checksum, builds the package, and installs it through pacman.

### Manual Installation

#### Bootstrap

Run the bootstrap script once to compile `sk` with your system C compiler:

```bash
./scripts/build_linux.sh
```

Alternatively, use the Python build script:

```bash
python3 ./scripts/build.py
```

#### Self-build and Optional Installation

Once bootstrapped, `sk` can rebuild itself via its own `Stormfile`.

Installation is optional and can be configured to place the binary in a user-owned directory:

- **Example Install Location:** `~/.local/bin`
- **Custom Install Location:** Edit the `install` keyword value inside the `Stormfile` to specify another user directory.

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
sk -C myproject init strike --profile 
```

---

## In practice

`sk` builds itself: 

```
sk strike --profile
[Stormfile]: __sk_version__ = 0.15.0
[sk]: Generated: src/core/sk_config.h
[sk]: Cache: 0 hits, 0 compiled, 38 unchanged: 38 total
[sk]: Nothing to compile, cache and files up-to-date
[sk]: Target (sk) up-to-date, skipping link
====== Profiler ======
Lexer  : 36.24 us
Parser : 1.65 ms
Eval   : 1.83 ms
Compile: 33.95 ms
Link   : 96.61 us
Strike : 48.97 ms
======================
Total: 49.13 ms
======================
```

```
sk status
================================= STORM-KNELL STATUS ==============================================
  Target Name             Kind      Status Check          Total Files     Size          Age         
  ------------------------------------------------------------------------------------------------
  ✔ sk                    EXEC      [OPERATIONAL]         38              204.59 KB     8s ago      
  ------------------------------------------------------------------------------------------------
  Cache Summary    :  38 hits, 0 misses (Total Ops: 38, 100.0% cached)
  Total Footprint  :  204.59 KB
  Workspace Status : READY / HEALTHY
==================================================================================================
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
| `--force` | Force action |
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
***NOTE:*** For more Stormfile examples see: [docs/examples-1.md](docs/examples-1.md)

---

## Passing Arguments to Surged Targets

Use `:::` to separate `sk` arguments from arguments passed to the target binary.

Everything after `:::` is forwarded to the target binary executed by `surge`.

```bash
sk strike surge ::: --my-app-flag --verbose
sk clean strike surge ::: -h
```

## Executing Targets with Tools

Use `--tool='<tool> [args...]'` before specifying a target to run it under an external tool.

The value is split into words using shell-style quoting (single quotes, double quotes, backslash escapes). sk runs the tool directly, not through `/bin/sh`, so `;`, `&&`, `$VAR` and `*` inside the value reach the tool as literal text. Your own shell still processes the command line first, so wrap the whole `--tool` value in quotes. Only one `--tool` is allowed per invocation.

```bash
# Disassemble the target
sk --tool='objdump -d -M intel' surge my_app

# List dynamic symbols
sk --tool='nm -D' surge my_app

# Check for leaks
sk --tool='valgrind --tool=memcheck --leak-check=full' surge my_app

# Trace file access
sk --tool='strace -f -e trace=openat' surge my_app

# Quote an argument that contains spaces
sk --tool='gdb -batch -ex "info functions"' surge my_app
```

**Target arguments:** `:::` separates the tool invocation from arguments for the target binary. They are appended after the target path, so the tool receives them. Whether they reach the target depends on the tool.

```bash
# valgrind runs the target, so --help reaches it
sk --tool=valgrind surge my_app ::: --help
# runs: valgrind <bin> --help

# gdb needs --args to forward them to the target
sk --tool='gdb -batch -ex run --args' surge my_app ::: --verbose
# runs: gdb -batch -ex run --args <bin> --verbose

# nm only inspects the file, so --help goes to nm
sk --tool=nm surge my_app ::: --help
# runs: nm <bin> --help
```

**Exit status:** sk exits with the tool's exit code. If the tool is killed by a signal, sk exits with `128 + signal` (139 for SIGSEGV) and logs the signal name.

---

## Caching

sk maintains a global object cache keyed on source content, compiler version, sk version, cflags, includes, and defines. Unchanged translation units are never recompiled.

```bash
sk cache          # view cache size
sk cache --nuke   # clear cache
```

---

## Benchmark

A detailed benchmark against CMake + Ninja using the Lua 5.5 source tree is available in [docs/benchmark/BENCHMARK.md](docs/benchmark/BENCHMARK.md).

The benchmark covers clean builds, unchanged rebuilds, timestamp-only changes, modified sources, and Storm-Knell's project-local artifact removal with its global object cache retained

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

### CTags
To generate `TAGS` with etags format pass `--gen-tags` flag during a strike:

```bash
sk strike --gen-tags
```

### Visual Studio Code
To generate `.vscode/tasks.json` with **Strike** (Ctrl+Shift+B) and **Surge** tasks:
```bash
sk --vscode
```
Errors from the compiler will be highlighted inline via the `$gcc` problem matcher.

---

## License

[MIT](LICENSE)
