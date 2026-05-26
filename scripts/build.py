#!/usr/bin/env python3
import os
import sys
import subprocess
import shutil
import time

def get_c23_flag(cc):
    try:
        result = subprocess.run([cc, "--version"], capture_output=True, text=True)
        # gcc 14+ uses c23, older uses c2x
        if "gcc" in cc or "gcc" in result.stdout.lower():
            ver = subprocess.run([cc, "-dumpversion"], capture_output=True, text=True)
            major = int(ver.stdout.strip().split(".")[0])
            return "-std=c23" if major >= 14 else "-std=c2x"
    except:
        pass
    return "-std=c23"

def main():
    if os.name != 'nt' and os.getuid() == 0:
        print("[build.py]: Error: Do not run the bootstrap as root.")
        sys.exit(1)

    start_time = time.time()
    build_dir = "bootstrap_stage"
    os.makedirs(build_dir, exist_ok=True)

    cc = None
    ldflags = []
    
    if shutil.which("clang"):
        cc = "clang"
        if shutil.which("mold") and os.name == 'posix':
            ldflags.append("-fuse-ld=mold")
    elif shutil.which("gcc"):
        cc = "gcc"
    elif shutil.which("cc"):
        cc = "cc"

    if not cc:
        print("[build.py]: Error: No compatible C compiler found (checked clang, gcc, cc).")
        sys.exit(1)

    print(f"[build.py]: Bootstrapping toolchain with {cc}...")

    include_paths = [
        "src",
        "src/core",
        "src/core/cli",
        "src/core/commands",
        "src/core/dsl",
        "src/core/hash",
        "src/core/invoke",
        "src/core/memory",
        "src/core/sk_cache",
        "src/core/util",
        "external/vx/include"
    ]

    sources = []
    for root_dir in ["src", "external/vx/src"]:
        if os.path.exists(root_dir):
            for root, _, files in os.walk(root_dir):
                for file in files:
                    if file.endswith(".c"):
                        sources.append(os.path.join(root, file))

    out_exe = os.path.join(build_dir, "sk_bootstrap" + (".exe" if os.name == 'nt' else ""))

    args = [cc, get_c23_flag(cc), "-O1", "-Wall", "-Wextra", "-Werror"]

    if sys.platform == "linux":
        args.append("-D_GNU_SOURCE")

    args.extend([f"-I{p}" for p in include_paths])
    args.extend(ldflags)
    args.extend(sources)
    args.extend(["-lxxhash", "-lpthread", "-o", out_exe])

    try:
        subprocess.run(args, check=True)
    except subprocess.CalledProcessError:
        print("[build.py]: Error: Staging compilation failed.")
        sys.exit(1)

    if os.path.exists(out_exe):
        print("[build.py]: Bootstrap driver ready. Handing over build execution to Storm-Knell...")
        print()
        
        try:
            if os.name != 'nt':
                cmd = [f"./{out_exe}", "init", "strike", "--profile", "--set=bootstrap"]
            else:
                cmd = [out_exe, "init", "strike", "--profile", "--set=bootstrap"]

            subprocess.run(cmd, check=True)
        except subprocess.CalledProcessError:
            print("[build.py]: Error: Storm-Knell native build process failed.")
            sys.exit(1)
        finally:
            print()
            print("[build.py]: Definitive build completed successfully. Cleaning staging directory...")
            shutil.rmtree(build_dir, ignore_errors=True)
    else:
        print("[build.py]: Error: Bootstrap binary was not generated.")
        sys.exit(1)

    print("--------------------------------------")
    print(f"[build.py]: Total bootstrap process took {time.time() - start_time:.2f}s")

if __name__ == "__main__":
    main()
