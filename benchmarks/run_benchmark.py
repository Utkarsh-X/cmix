import argparse, os, sys, time, hashlib, subprocess, json

GOLDEN_HASHES = {
    "test_15k.dat": "A7C22EFF48842CE33DA9193B0DBFF2B5EDA5BD694BD7D515C11E288779706DD0",
    "enwik8_100kb": "581BEE9758C531DE3795885A1ADB331B9C51BFDEEFD9A63407815479E22A9072",
    "enwik8_1mb":   "369B688978F649681136198FB96DB14C1616756260C55FB4B65E9BC049552CAD",
}

RECORDS = {
    "test_15k.dat": 2983,      # EXP-069 World Record
    "enwik8_100kb": 19330,     # EXP-069 World Record
    "enwik8_1mb":   171537,    # EXP-067 World Record
}

def run_pass(cmix_bin, dict_file, mode_flag, in_file, out_file, is_decomp=False):
    cmd = [cmix_bin, mode_flag, dict_file, in_file, out_file]
    t0 = time.time()
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
    last_print = time.time()
    for line in iter(p.stdout.readline, ''):
        now = time.time()
        line_s = line.strip()
        if "progress:" in line_s or "cross entropy:" in line_s or "pretraining:" in line_s:
            if now - last_print >= 10 or "100.00%" in line_s:
                print(line_s, flush=True)
                last_print = now
        elif line_s:
            print(line_s, flush=True)
    p.wait()
    dt = time.time() - t0
    return p.returncode, dt

def run_tier(name, input_file, exp_id, cmix_bin, dict_file, scratch_dir):
    filename = os.path.basename(input_file)
    expected_hash = GOLDEN_HASHES.get(filename)
    record_size = RECORDS.get(filename, 0)
    
    comp_file = os.path.join(scratch_dir, f"{filename}_{exp_id}.cmix")
    decomp_file = os.path.join(scratch_dir, f"{filename}_{exp_id}_decomp.dat")
    temp_file = comp_file + ".cmix.temp"
    
    for f in [comp_file, decomp_file, temp_file]:
        if os.path.exists(f):
            try: os.remove(f)
            except OSError: pass

    print(f"\n{'='*20} {name}: {filename} ({exp_id}) {'='*20}", flush=True)
    print(f"Compressing {filename} with dictionary...", flush=True)
    ret_c, dt_c = run_pass(cmix_bin, dict_file, "-c", input_file, comp_file)
    if ret_c != 0 or not os.path.exists(comp_file):
        print(f">>> COMPRESSION FAILED! (Exit code: {ret_c}) <<<", flush=True)
        return {"pass": False, "error": f"compress_exit_{ret_c}"}
        
    comp_size = os.path.getsize(comp_file)
    delta = comp_size - record_size
    print(f"Compressed size: {comp_size} bytes (Reigning record: {record_size} bytes, delta: {delta:+d} B, time: {dt_c:.2f}s)", flush=True)

    print("Executing decompression for 100% bit-exact verification...", flush=True)
    ret_d, dt_d = run_pass(cmix_bin, dict_file, "-d", comp_file, decomp_file, is_decomp=True)
    if ret_d != 0 or not os.path.exists(decomp_file):
        print(f">>> DECOMPRESSION FAILED! (Exit code: {ret_d}) <<<", flush=True)
        return {"pass": False, "comp_size": comp_size, "delta": delta, "error": f"decompress_exit_{ret_d}"}

    with open(decomp_file, "rb") as f:
        h = hashlib.sha256(f.read()).hexdigest().upper()
    print(f"Decompressed SHA-256: {h}", flush=True)
    
    hash_match = (h == expected_hash)
    if hash_match:
        print(f">>> {name} 100% BIT-EXACT MATCH PASS! <<<", flush=True)
    else:
        print(f">>> {name} SHA-256 MISMATCH! Expected: {expected_hash} <<<", flush=True)

    return {
        "pass": hash_match,
        "comp_size": comp_size,
        "record_size": record_size,
        "delta": delta,
        "compress_time_sec": dt_c,
        "decompress_time_sec": dt_d,
        "decomp_sha256": h,
        "sha256_match": hash_match
    }

def main():
    parser = argparse.ArgumentParser(description="CMIX Benchmark Runner")
    parser.add_argument("--tier", default="tier1", choices=["tier1", "tier2", "tier3", "all"])
    parser.add_argument("--exp", default="EXP-TEST")
    parser.add_argument("--bin", default="cmix_v21.exe")
    parser.add_argument("--dict", default=os.path.join("dictionary", "english.dic"))
    parser.add_argument("--data-dir", default=os.path.join("benchmarks", "data"))
    parser.add_argument("--scratch-dir", default="scratch")
    args = parser.parse_args()

    os.makedirs(args.scratch_dir, exist_ok=True)
    
    results = {"experiment": args.exp, "tier": args.tier, "timestamp": time.time(), "tiers": {}}
    tiers_to_run = []
    if args.tier in ["tier1", "all"]:
        tiers_to_run.append(("Tier 1 (15KB)", os.path.join(args.data_dir, "test_15k.dat")))
    if args.tier in ["tier2", "all"]:
        tiers_to_run.append(("Tier 2 (100KB)", os.path.join(args.data_dir, "enwik8_100kb")))
    if args.tier in ["tier3", "all"]:
        tiers_to_run.append(("Tier 3 (1MB)", os.path.join(args.data_dir, "enwik8_1mb")))

    overall_pass = True
    for name, path in tiers_to_run:
        res = run_tier(name, path, args.exp, args.bin, args.dict, args.scratch_dir)
        results["tiers"][name] = res
        if not res.get("pass", False):
            overall_pass = False
            print(f">>> Aborting subsequent tiers due to failure in {name} <<<", flush=True)
            break

    results["overall_pass"] = overall_pass
    summary_path = os.path.join(args.scratch_dir, "summary.json")
    with open(summary_path, "w") as f:
        json.dump(results, f, indent=2)
    print(f"\nWrote benchmark summary to {summary_path}", flush=True)

    if not overall_pass:
        sys.exit(1)

if __name__ == "__main__":
    main()
