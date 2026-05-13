import random

prefixes = ["alpha", "beta", "gamma", "delta", "omega", "zeta"]
modes = ["debug", "release", "profile", "rel", "testing"]
kinds = ["exec", "static", "shared"]
custom_dirs = ["aloha", "dist", "build", "artifacts", "temp_out", "vendor"]

print("// #Stormfile - 32 Target Stress Test")
print("cc: clang")
print("linker: mold")
print("cflags: -Wall -Wextra")
print("mode: debug\n")

for i in range(1, 33):
    name = f"{random.choice(prefixes)}_{i}"
    kind = kinds[i % 3]
    mode = modes[i % 5]
    
    print(f"target {name} {{")
    print(f"    kind: {kind}")
    print(f"    mode: {mode}")
    
    # Randomly override out_dir every few targets
    if i % 3 == 0:
        print(f"    out_dir: {random.choice(custom_dirs)}_{i}")
    
    print(f"    sources: src/")
    print(f"    cflags:: -O{i % 4} -DSTR_ID={i}")
    
    # Add some dummy dependencies to test the depend_count/array
    if i > 1:
        prev_target = f"{random.choice(prefixes)}_{i-1}"
        print(f"    depends: {prev_target}")
        
    print("}\n")
