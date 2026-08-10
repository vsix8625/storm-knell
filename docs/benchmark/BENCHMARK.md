# Benchmark

## Results

| Scenario                            | CMake + Ninja | Storm-Knell |
| ----------------------------------- | :-----------: | :---------: |
| Configuration                       |        0.58 s |      0.00 s* |
| Clean build                         |        9.36 s |      9.13 s |
| Unchanged rebuild                   |        0.06 s |      0.03 s |
| Touch `lapi.c` (contents unchanged) |        1.21 s |      0.03 s |
| Modify `lapi.c`                     |        1.27 s |      1.19 s |
| Project artifacts removed           |             — |      0.19 s |

* Storm-Knell does not have a separate configuration/generation step.
  `sk strike` parses the Stormfile and proceeds directly to the build.
  
This benchmark compares Storm-Knell against CMake + Ninja using the same Lua source tree and equivalent Release build configurations.

The benchmark focuses on several build states:

- Configuration
- Clean build
- Unchanged rebuild
- Touched source with unchanged contents
- Actually modified source
- Project artifacts removed while Storm-Knell's global cache remains populated

All timings were measured with `/usr/bin/time -v` using its `Elapsed (wall clock) time` value.

## Test Environment

```text
Architecture: x86_64
CPU: AMD A6-7310 APU with AMD Radeon R4 Graphics
Cores: 4
RAM: 10 GB
Storage: HDD
````

## Test Project

The benchmark uses the Lua source tree at commit
`4cf498210e6a60637a7abb06d32460ec21efdbdc`.

Source repository: https://github.com/lua/lua

```bash
git clone https://github.com/lua/lua.git
cd lua
git checkout 4cf498210e6a60637a7abb06d32460ec21efdbdc
```

Both build systems use:

* Clang
* lld
* Release configuration
* The same source files
* The same libraries
* Parallel compilation

The build files used for the benchmark are included in this repository:

* [`CMakeLists.txt`](CMakeLists.txt)
* [`Stormfile`](Stormfile)

Both builds produce equivalent Lua executables.

## Configuration

CMake requires a configuration/generation step before building with Ninja:

```bash
/usr/bin/time -v cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

Elapsed time:

```text
0.58 s
```

Storm-Knell does not have a separate configuration/generation step. The `Stormfile` is evaluated as part of `sk strike`.

## Clean Build

Both builds were started without usable project-local compiled objects.

### CMake + Ninja

```bash
/usr/bin/time -v cmake --build build
```

All 33 translation units were compiled and the executable was linked.

Elapsed time:

```text
9.36 s
```

### Storm-Knell

The project cache was cleared before the test:

```bash
sk clean --full
```

Then:

```bash
/usr/bin/time -v sk strike
```

Result:

```text
[sk]: Cache: 0 hits, 33 compiled, 0 unchanged: 33 total
[sk]: Linking target: lua_sk
```

Elapsed time:

```text
9.13 s
```

## Unchanged Rebuild

After the clean build, both projects were already up to date.

### CMake + Ninja

```bash
/usr/bin/time -v cmake --build build
```

```text
ninja: no work to do.
```

Elapsed time:

```text
0.06 s
```

### Storm-Knell

```bash
/usr/bin/time -v sk strike
```

```text
[sk]: Cache: 0 hits, 0 compiled, 33 unchanged: 33 total
[sk]: Nothing to compile, cache and files up-to-date
[sk]: Target (lua_sk) up-to-date, skipping link
```

Elapsed time:

```text
0.03 s
```

## Touched Source — Contents Unchanged

`lapi.c` was touched without changing its contents:

```bash
touch lapi.c
```

This changes the file modification time but not the source contents.

### CMake + Ninja

Ninja rebuilt `lapi.c.o` and linked the executable.

Elapsed time:

```text
1.21 s
```

### Storm-Knell

Storm-Knell detected that the source contents were unchanged:

```text
[sk]: Cache: 0 hits, 0 compiled, 33 unchanged: 33 total
[sk]: Nothing to compile, cache and files up-to-date
[sk]: Target (lua_sk) up-to-date, skipping link
```

Elapsed time:

```text
0.03 s
```

## Modified Source

`lapi.c` was then actually modified.

### CMake + Ninja

Ninja rebuilt `lapi.c.o` and linked the executable.

Elapsed time:

```text
1.27 s
```

### Storm-Knell

Storm-Knell rebuilt only the changed translation unit:

```text
[1]: lapi.c
[sk]: Cache: 0 hits, 1 compiled, 32 unchanged: 33 total
[sk]: Linking target: lua_sk
```

Elapsed time:

```text
1.19 s
```

## Project Artifacts Removed

Storm-Knell maintains a global content-addressed object cache.

After the project-local build artifacts were removed, the cached objects were still available.

Running:

```bash
/usr/bin/time -v sk strike
```

produced:

```text
[sk]: Cache: 33 hits, 0 compiled, 0 unchanged: 33 total
[sk]: Linking target: lua_sk
```

Elapsed time:

```text
0.19 s
```

This is **not a clean-build comparison**. The translation units were not recompiled because their objects were already present in Storm-Knell's global cache.

It demonstrates the behavior of reconstructing project-local build artifacts from the global cache.


