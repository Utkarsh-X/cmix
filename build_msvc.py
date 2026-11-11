import os
import sys
import glob
import shutil
import subprocess
import time
from concurrent.futures import ThreadPoolExecutor

VCVARS = r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

def get_msvc_env():
    cmd = f'call "{VCVARS}" && set'
    out = subprocess.check_output(cmd, shell=True, text=True, errors="ignore")
    env = {}
    for line in out.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            env[k] = v
    return env

def compile_file(args):
    cl_exe, cpp_file, obj_file, env, flags, inc = args
    cmd = [cl_exe, "/nologo", *flags, f"/I{inc}", f"/I{os.path.join(inc, 'src')}", "/c", cpp_file, f"/Fo{obj_file}"]
    t0 = time.time()
    res = subprocess.run(cmd, env=env, capture_output=True, text=True)
    el = time.time() - t0
    return cpp_file, obj_file, res.returncode, res.stdout, res.stderr, el

def main():
    print("Initializing MSVC 2022 x64 environment...")
    env = get_msvc_env()
    print("Environment loaded.")

    base_dir = os.path.dirname(os.path.abspath(__file__))
    src_dir = os.path.join(base_dir, "src")
    build_dir = os.path.join(base_dir, "build_msvc")
    os.makedirs(build_dir, exist_ok=True)

    all_cpps = glob.glob(os.path.join(src_dir, "**", "*.cpp"), recursive=True)
    cmix_cpps = [
        f for f in all_cpps
        if not ("enwik9-preproc" in f)
    ]

    cl_exe = shutil.which("cl", path=env["PATH"])
    link_exe = shutil.which("link", path=env["PATH"])
    if not cl_exe or not link_exe:
        print("Error: cl.exe or link.exe not found in MSVC environment PATH!")
        sys.exit(1)
    print(f"Using compiler: {cl_exe}")
    print(f"Using linker:   {link_exe}")

    flags = [
        "/std:c++17",
        "/EHsc",
        "/O2",
        "/arch:AVX2",
        "/GL",
        "/Gw",
        "/Gy",
        "/DNDEBUG",
        f"/FI{os.path.join(base_dir, 'msvc_compat.h')}",
        "/DP1_MODE=2",
        "/DP3_BATCH",
        "/DP3_REGBLOCK",
        "/DOERR_REGBLOCK",
        "/DP2_FASTMATH",
        "/DLSTM_QUANT=8",
        "/DOUTPUT_LR=0.06f",
        "/W3",
        "/wd4244", # conversion loss of data
        "/wd4267", # conversion from size_t
        "/wd4996", # deprecated functions
        "/wd4305", # truncation from double to float
    ]

    tasks = []
    for cpp in cmix_cpps:
        rel = os.path.relpath(cpp, src_dir)
        obj_name = rel.replace(os.sep, "_").replace(".cpp", ".obj")
        obj_path = os.path.join(build_dir, obj_name)
        tasks.append((cl_exe, cpp, obj_path, env, flags, base_dir))

    print(f"Compiling {len(tasks)} C++ translation units with {os.cpu_count()} parallel threads...")
    t_start = time.time()
    obj_files = []
    errors = []

    with ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
        for cpp, obj, code, stdout, stderr, el in pool.map(compile_file, tasks):
            rel = os.path.relpath(cpp, base_dir)
            if code == 0:
                print(f"  [OK] {rel} ({el:.2f}s)")
                obj_files.append(obj)
            else:
                print(f"  [FAIL] {rel} (Exit {code})")
                print(stdout)
                print(stderr)
                errors.append(cpp)

    if errors:
        print(f"\nCompilation failed on {len(errors)} files!")
        sys.exit(1)

    print(f"All {len(obj_files)} objects compiled in {time.time() - t_start:.2f}s.")
    print("Linking cmix_v21.exe...")
    exe_path = os.path.join(base_dir, "cmix_v21.exe")
    link_cmd = [link_exe, "/nologo", "/LTCG", "/OPT:REF", "/OPT:ICF", "/OUT:" + exe_path, *obj_files]
    t0 = time.time()
    res = subprocess.run(link_cmd, env=env, capture_output=True, text=True)
    if res.returncode == 0:
        print(f"SUCCESS! Built '{exe_path}' in {time.time() - t0:.2f}s.")
    else:
        print("Linker error:")
        print(res.stdout)
        print(res.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
