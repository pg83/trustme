# Project style settings

Per-project settings that the shared [STYLE.md](STYLE.md) delegates here.
They cover the compiler tree. `bin/cargo` is Go and follows
[bin/cargo/STYLE.md](bin/cargo/STYLE.md); `ext/libstd` is its own project
with its own copies of both files.

- **Macro prefix.** None reserved. The compiler keeps its inherited unprefixed
  diagnostic macros (`ASSERT`, `BUG`, `TODO`, ...).
- **Namespace.** The compiler is a program: no project namespace. Code keeps
  the module namespaces inherited from upstream (`AST`, `HIR`, `MIR`, ...).
- **Formatter.** `./dev/style.py` formats every tracked C++ source except
  `ext/`, which formats itself.
- **Unit tests.** New compiler modules pair `x.h`/`x.cpp` with an `x_ut.cpp`
  next to them, written with libstd's `STD_TEST` framework. Every
  `bin/rustc/*_ut.cpp` is linked into the `rustc_ut` runner (built and run by
  the `unit_rustc_ut` node); methods are implemented in the `.cpp`, not the
  header.

## Deviations

`bin/rustc` is inherited code, but the naming migration is done: the
tree uses house names — `UpperCamelCase` types, `lowerCamelCase` functions and
methods, `lowerCamelCase_` private data members, unprefixed public fields,
`snake_case` filenames. No `m` prefix survives. New code follows
[STYLE.md](STYLE.md) as written; there is no "match the surrounding spelling"
exemption.
