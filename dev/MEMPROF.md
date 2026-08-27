# Memory-profiling the compiler

How to find the lines of `bin/rustc` that allocate the most — by bytes and
by allocation count — using a heap profile of a real workload: compiling
`libcore` from the standard-library source, with the compiler invoked
directly (no cargo, no C backend).

Validated 2026-08-18 in the ix/clang environment. Every step below was run
end to end; the numbers in the appendix come from that run.

## 1. Get a workdir with the rust source

The `std_src` graph node has already downloaded and patched the tree; reuse
its tar instead of downloading again:

```sh
W=/tmp/memprof-work            # anywhere with ~10 GB free
mkdir -p $W && tar -C $W -xf $(find .build -name rust-src.tar | head -1)
```

After this `$W/library/core/src/lib.rs` exists.

## 2. Build a profiling compiler

The normal link uses `-ltcmalloc_minimal`, which has **no heap profiler**.
The full library lives in the `lib-gperftools-profile` store package, and it
must be linked `--whole-archive`: the profiler hooks itself in through
static initialisers that an ordinary archive link (plus `--gc-sections`)
drops silently — the binary links fine and `HEAPPROFILE` just does nothing.
Check with `llvm-nm rustc-prof | grep -c HeapProfiler` (zero = you got the
silent failure).

Two engine gotchas make the by-hand route the reliable one:

- the build engine appends the environment `LDFLAGS` *after* whatever your
  `CXX` wrapper adds, so the minimal tcmalloc sneaks back in;
- the engine hashes the wrapper *path*, not its content — editing a wrapper
  does not invalidate the cached link.

So compile and link outside the engine. First generate the build-time
inputs, then compile every TU, then link with a wrapper that swaps the
allocator:

```sh
S=$PWD; G=$W/gen; mkdir -p $G $W/objs
for tu in bin/rustc/*.tu; do
    b=$(basename $tu .tu)
    python3 dev/tu_gen.py $tu $G/${b}_tu.h $G/${b}_tu.cpp
done
python3 dev/embed_text.py bin/rustc/prelude.inc $G/codegen_c_prelude.h CODEGEN_C_PRELUDE
python3 dev/gen_unicode_nfc.py $G/unicode_nfc_tables.inc

# c++ = the usual realm wrapper (clang + realm CFLAGS/CPPFLAGS/LDFLAGS).
FLAGS="-std=c++26 -O2 -g -I bin/rustc -I $G -I ext/libstd
       -DVERSION_GIT_ISDIRTY=0 -DVERSION_GIT_FULLHASH=\"\\\"u\\\"\"
       -DVERSION_GIT_SHORTHASH=\"\\\"t\\\"\" -DVERSION_BUILDTIME=\"\\\"u\\\"\"
       -DVERSION_GIT_BRANCH=\"\\\"m\\\"\""
ls bin/rustc/*.cpp $G/*_tu.cpp | grep -v '_ut.cpp\|sample' |
    xargs -P $(nproc) -I{} sh -c "c++ $FLAGS -c {} -o $W/objs/\$(basename {} .cpp).o"

# Link wrapper: same as c++, but LDFLAGS get
#   s|-ltcmalloc_minimal|-Wl,--whole-archive <profile>/libtcmalloc.a -Wl,--no-whole-archive|
PROF=$(ls -d /ix/store/*-lib-gperftools-profile | head -1)
c++prof -o $W/rustc-prof $W/objs/*.o \
    $(find .build -name libstd.a | head -1) -lz
llvm-nm $W/rustc-prof | grep -c HeapProfiler   # must be > 0
```

(`c++prof` is the two-line wrapper with the sed applied; see the realm
wrapper recipe. The platform `libstd.a` comes from any recent build dir.)

## 3. Profile a direct libcore compile

`-C emit-build-command=` makes the compiler emit the C-codegen script
instead of running the C compiler, so no `CC/CXX` is needed and the run
measures only the compiler itself:

```sh
export HEAPPROFILE=$W/core \
       TCMALLOC_STACKTRACE_METHOD=generic_fp \
       HEAP_PROFILE_ALLOCATION_INTERVAL=1099511627776 \
       HEAP_PROFILE_INUSE_INTERVAL=0
$W/rustc-prof $W/library/core/src/lib.rs -o $W/libcore.rlib -O -L $W \
    --crate-name core --crate-type rlib --crate-tag 0_0_0 \
    -C emit-build-command=$W/libcore-codegen.sh --edition 2024
```

- `TCMALLOC_STACKTRACE_METHOD=generic_fp` is **essential**. The library
  defaults to the libgcc DWARF unwinder, which makes the profiled run ~100x
  slower (Expand alone did not finish in ten minutes); with the
  frame-pointer walker the whole compile profiles in about three minutes.
  Our binaries always build with `-fno-omit-frame-pointer`, so it is safe.
- The huge `ALLOCATION_INTERVAL` suppresses intermediate dumps; the profile
  you want is the single cumulative dump written at exit:
  `$W/core.0001.heap` (~700 MB of text for libcore).

## 4. Aggregate by source line

```sh
python3 dev/heap_agg.py $W/core.0001.heap $W/rustc-prof
```

This prints the total churn plus three tables: top lines of `bin/rustc` by
cumulative allocated bytes, the same by allocation count, and an
"infrastructure view" that keeps ThinVector/RcString frames (useful for
seeing how much of the total funnels through container growth). Attribution
is "first stack frame inside `bin/rustc`", i.e. the same thing
`pprof -top -lines` would show filtered to our code.

`go tool pprof` itself understands the dump format but chokes on dumps this
size (30+ minutes with no visible progress on 700 MB); `heap_agg.py` +
batch `llvm-symbolizer` processes it in seconds. If you want the
interactive pprof UI, profile something smaller or cut the dump down first.

## Appendix: libcore, 2026-08-18

Total churn for one `libcore` compile: **9.38 GB / 150M allocations**.
Leaders:

| MB | M allocs | line | what |
|---:|---:|---|---|
| 556 | 11.9 | `hir_path.cpp:124` | `HIRSimplePath::clone()` copies the member vector; the header's own TODO asks for dedup |
| 543 | 17.0 | `mir_helpers.cpp:664` | a `std::function` wrapper allocated per `visitTerminatorTarget` call |
| 289 | 6.4 | `hir_path.h:73` | `HIRSimplePath` construction |
| 268 | ~0 | `hir_from_ast.cpp:2473` | pool chunk growth behind `_add_mod_val_item` |
| 247 | 0.1 | `mir_from_hir.cpp:9516` | `MIRFunction::blocks` growth in `newBbUnlinked` |
| >1200 | — | `mir_operations.cpp` (many) | fresh per-pass vectors in the `MIROptimise*` family |
| 185 | 1.4+ | `rc_string.cpp:64` + retired logger | formatting allocated in release even when discarded |

One third of everything (1.25 GB / 29.7M) flows through
`ThinVector::reserveInit` — mostly the same call sites as above.
