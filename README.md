# Storm-Knell (sk)

A fast, cache-aware build tool with its own DSL — describe your project once, let sk figure out the rest.

---

## Requirements

- GCC or Clang (C23)
- [xxhash](https://github.com/Cyan4973/xxHash)
- [mold](https://github.com/rui314/mold) *(recommended, but not required)*
- pthreads *(usually ships with your system)*

## Installation

### From source (recommended)

```bash
git clone --recurse-submodules https://github.com/yourusername/storm-knell.git
cd storm-knell
./scripts/build.sh
```

The bootstrap script compiles sk using your system compiler. Once built, sk rebuilds itself using its own Stormfile and installs to `~/.local/bin`.

### Prebuilt binaries

Download the latest binary for your platform from [Releases](https://github.com/yourusername/storm-knell/releases).

```bash
chmod +x sk
mv sk ~/.local/bin/
```

## Platform support

| Platform | Status |
|---|---|
| Linux x86_64 | Tested |
| Windows | Untested |

## Quick start

Initialize sk in your project:

```bash
sk init
```

This creates a starter `Stormfile`. Edit it to match your project, then build:

```bash
sk strike
```

## Basic Stormfile

```
cc: /usr/bin/gcc
linker: mold
cflags: -std=c23 -Wall -Werror
lflags: -lpthread

target myapp
{
    out: myapp
    mode: release
    cflags:: -O2
    sources:
    src/
    includes:
    -Isrc
    install: ~/.local/bin
}
```

## Commands

| Command | Description |
|---|---|
| `sk strike` | Parse Stormfile and build project |
| `sk surge` | Run target |
| `sk clean` | Clean build artifacts |
| `sk init` | Initialize sk in working directory |
| `sk purge` | De-initialize sk from working directory |
| `sk cache` | View or clean the global cache |
| `sk status` | View project status |

## Global options

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

## CLI variable injection

Pass boolean variables into your Stormfile at build time without modifying it:

```bash
sk --set=lto strike
sk --set=asan --set=debug_tracy strike
```

In your Stormfile:

```
if(lto)
{
    cflags:: -O3 -flto=auto
    lflags:: -flto=auto -Wl,-O1 -Wl,--as-needed
}
```

## Caching

sk maintains a global object cache keyed on source content, compiler version, sk version, cflags, includes, and defines. Unchanged translation units are never recompiled.

```bash
sk cache          # view cache size
sk cache --nuke   # clear cache
```

## License

[MIT](LICENSE)
