# rustc

A hard fork of [mrustc](https://github.com/thepowersgang/mrustc) with a
different goal.

Upstream mrustc is a bootstrapping seed: a C++ program that compiles Rust just
well enough to build the real `rustc` once, so the official compiler can take
over. We are not bootstrapping. We are turning mrustc into **a real, standalone
Rust compiler** — one that stays and is used to build actual projects. (The
compiler binary is therefore called `rustc`, not `mrustc`.)

## how we get there

Not by chasing a spec, but by making real programs compile and pass their tests.

Every real-world crate we throw at the compiler exposes bugs — in type
inference, in const evaluation, in the C codegen, in an intrinsic. We fix each
one in the compiler and lock it in with a small regression test, then add the
whole project as a standing test. The suite grows in two directions at once:

- **breadth** — more real binaries that build from source and pass their own
  tests (`resvg` is the first; more follow);
- **depth** — a one-file regression under `tst/unit/` for every compiler bug
  we fix along the way.

The compiler gets more correct exactly as fast as the set of programs it can
build gets larger. That is the whole plan: pile on more building, test-passing
binaries, fixing mrustc as we go.

## layout

```
bin/rustc/  the compiler sources (flat)
bin/cargo/  our cargo, rewritten in Go — vendors dependencies by lockfile
lib/        Rust libraries built with the toolchain
ext/libstd/ the external C++ platform library in the rustc graph
tst/        the test graph: build a real project, run its tests
dev/        plans and development utilities
build       the build engine (shared across the monorepo)
build.py    the build graph for everything above
```

`cargo` provides Cargo-compatible `build`, `test`, and `vendor` commands. It
resolves path, workspace, patched, target-specific, and vendored dependencies,
runs build scripts, and schedules compile/codegen jobs in parallel. The test
graph uses its archive extension (`cargo vendor -Zarchive=...`) to pass a
hermetic vendor tree between isolated nodes.

## building

```
./build                 # rustc + cargo
./build platform_libstd # just the vendored C++ platform library
./build unit            # the one-file compiler regressions
./build resvg           # build resvg from source and render-test it
./build test            # the fast semantic corpus
./build slow_tests      # resvg + unusually long upstream library tests
```

See [`tst/README.md`](tst/README.md) for how a project test is wired as a
graph of nodes (fetch → vendor → build against a from-source libstd → run).

### distributed builds

```
./build --dist=lab1,lab2 --dist=lab3:32 unit
```

`--dist` adds ssh hosts as workers. Each host gets one ssh session; the
engine sends itself over as `build exec` and then streams jobs to it. A job
carries its command, environment and the manifests of its declared inputs;
the host keeps a per-session content-addressed cache under `./.build-exec`
in the login directory, so every blob crosses the link at most once. Results
land in the local CAS exactly as if the node had run here. Dropped sessions
reconnect with backoff, in-flight nodes go back to the queue. `HOST:N`
overrides the slot count (default: the host's cores). Hosts run whatever the
login shell provides: the same toolchain paths as here are assumed, not
checked. A node that must stay on this machine (network fetches, anything
writing into the source tree) is declared with `local=True`. A remote host
only sees a node's declared inputs, so `--dist` also acts as a hermeticity
gate. Set `BUILD_DIST_SSH` to use another ssh command.

## license

mrustc is MIT licensed; see [`bin/rustc/LICENCE-MIT`](bin/rustc/LICENCE-MIT).
The platform library is MIT licensed; see
[`ext/libstd/LICENSE`](ext/libstd/LICENSE). Our changes carry
the same license.
