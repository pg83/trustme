# Project style settings

Per-project settings that the shared [STYLE.md](STYLE.md) delegates here.
They cover the compiler tree. `bin/cargo` is Go and follows
[bin/cargo/STYLE.md](bin/cargo/STYLE.md); `ext/libstd` is its own project
with its own copies of both files.

- **Macro prefix.** None reserved. The compiler keeps mrustc's unprefixed
  diagnostic macros (`ASSERT`, `BUG`, `TODO`, `DEBUG`, ...).
- **Namespace.** The compiler is a program: no project namespace. Code keeps
  the module namespaces inherited from mrustc (`AST`, `HIR`, `MIR`, ...).
- **Formatter.** `./dev/style.py` formats every tracked C++ source except
  `ext/`, which formats itself.
- **Unit tests.** New compiler modules pair `x.h`/`x.cpp` with an `x_ut.cpp`
  next to them, written with libstd's `STD_TEST` framework. Every
  `bin/rustc/*_ut.cpp` is linked into the `rustc_ut` runner (built and run by
  the `unit_rustc_ut` node); methods are implemented in the `.cpp`, not the
  header.

## Deviations

`bin/rustc` is inherited from mrustc and predates the house style. It keeps
upstream naming (`snake_case` functions, `m_`-prefixed data members) and uses
the C++ standard library throughout; libstd (`stl::`) is linked in and adopted
where it fits. New compiler code matches the surrounding code — the migration
to the house rules is incremental, not wholesale.
