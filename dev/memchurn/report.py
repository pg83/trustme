#!/usr/bin/env python3
import subprocess, sys, re, collections
SP = sys.argv[1]; BIN = sys.argv[2]; TOP = int(sys.argv[3]) if len(sys.argv) > 3 else 40
INFRA = ("thin_vector.h", "rc_string.h", "range_vec_map.h", "/std/", "/nix/store/", "<unknown>", "??")
maps = []; sites = {}
for line in open(f"{SP}/sites.txt"):
    if line.startswith("SITES"): continue
    m = re.match(r"^([0-9a-f]+)-([0-9a-f]+) (\S+) ([0-9a-f]+) \S+ \S+\s+(\S+)$", line.strip())
    if m: maps.append((int(m.group(1), 16), int(m.group(2), 16), int(m.group(4), 16), m.group(5))); continue
    parts = line.split(); sites[int(parts[0])] = [int(x, 16) for x in parts[2:]]
exe = [m for m in maps if m[3].endswith(BIN.split('/')[-1]) or BIN.split('/')[-1] in m[3]]
base = min(m[0] - m[2] for m in exe) if exe else 0
def in_exe(a): return any(m[0] <= a < m[1] for m in exe)
addrs = sorted({a for fr in sites.values() for a in fr if a and in_exe(a)})
sym = {}
proc = subprocess.run(["llvm-symbolizer", "--obj=" + BIN, "--functions=short", "--inlining=true"], input="\n".join(hex(a - base - 1) for a in addrs), capture_output=True, text=True)
sys.stderr.write(proc.stderr[:500])
blocks = proc.stdout.strip().split("\n\n")
for a, b in zip(addrs, blocks):
    ls = [x.strip() for x in b.split("\n")]
    sym[a] = [(ls[i], ls[i + 1] if i + 1 < len(ls) else "?") for i in range(0, len(ls) - 1, 2)]
def frame(a):
    if a in sym: return sym[a]
    for m in maps:
        if m[0] <= a < m[1]: return [(m[3].split("/")[-1].split(".so")[0], "<lib>")]
    return [("?", "<unknown>")]
def attribute(frames):
    named = [f for a in frames if a for f in frame(a)]
    inner = named[0] if named else ("?", "?")
    for fn, loc in named:
        if "bin/rustc" in loc and not any(k in loc for k in INFRA): return (fn, loc), inner
    return (named[-1] if named else ("?", "?")), inner
agg = {}
for line in open(f"{SP}/agg.txt"):
    if line.startswith("site"): continue
    v = [int(x) for x in line.split()]
    agg[v[0]] = v[1:]
by = collections.defaultdict(lambda: [0] * 9); kinds = collections.defaultdict(collections.Counter)
tot = [0] * 9
for s, v in agg.items():
    (fn, loc), inner = attribute(sites.get(s, []))
    key = f"{loc.split('/')[-1]} {fn}"[:110]
    acc = by[key]
    for i in range(9): acc[i] += v[i]; tot[i] += v[i]
    kinds[key][inner[0][:40]] += v[0]
def row(k, v):
    count, byts, freed, sumd, l100, l10k, l1m, gt1m, live = v
    never = count - freed
    return f"{count/1e6:7.2f}M {byts/1e6:9.1f}MB  <=100:{100*l100/count:3.0f}% <=10k:{100*l10k/count:3.0f}% <=1M:{100*l1m/count:3.0f}% long:{100*gt1m/count:3.0f}% never:{100*never/count:3.0f}%  live@exit {live/1e6:7.1f}MB  {k}  [{', '.join(f'{n}:{c*100//count}%' for n, c in kinds[k].most_common(2))}]"
print(f"TOTAL allocs {tot[0]/1e6:.1f}M bytes {tot[1]/1e9:.2f}GB  freed<=100:{100*tot[4]/tot[0]:.0f}% <=10k:{100*tot[5]/tot[0]:.0f}% <=1M:{100*tot[6]/tot[0]:.0f}% long:{100*tot[7]/tot[0]:.0f}% never:{100*(tot[0]-tot[2])/tot[0]:.0f}%  live@exit {tot[8]/1e6:.0f}MB\n")
print("== by allocation count ==")
for k, v in sorted(by.items(), key=lambda kv: -kv[1][0])[:TOP]: print(row(k, v))
print("\n== by bytes ==")
for k, v in sorted(by.items(), key=lambda kv: -kv[1][1])[:TOP // 2]: print(row(k, v))
print("\n== by short-lived count (freed within 10k allocations) ==")
for k, v in sorted(by.items(), key=lambda kv: -(kv[1][4] + kv[1][5]))[:TOP // 2]: print(row(k, v))
print("\n== by live bytes at exit ==")
for k, v in sorted(by.items(), key=lambda kv: -kv[1][8])[:TOP // 2]: print(row(k, v))
print("\n== by source file ==")
byfile = collections.defaultdict(lambda: [0] * 9)
for k, v in by.items():
    f = k.split(":")[0]
    for i in range(9): byfile[f][i] += v[i]
for k, v in sorted(byfile.items(), key=lambda kv: -kv[1][0])[:25]:
    count, byts, freed, sumd, l100, l10k, l1m, gt1m, live = v
    print(f"{count/1e6:7.2f}M {byts/1e6:9.1f}MB  <=100:{100*l100/count:3.0f}% <=10k:{100*l10k/count:3.0f}% long:{100*gt1m/count:3.0f}% never:{100*(count-freed)/count:3.0f}%  live@exit {live/1e6:7.1f}MB  {k}")
