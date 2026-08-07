Приоритет я бы поставил так — по severity и числу разблокируемых тестов.

### P0 — сначала убрать ложные сигналы и зависания

- Исправить doctest extractor и проверять каждый результат эталонным Rust 1.90 перед импортом. Сейчас среди 547 failures есть заведомо сломанные inputs:
  - [prose и вложенный Markdown](/home/pg/monorepo/trustme/tests/rust_doctest/upstream/std/io/mod__L1613_runtime.rs:2);
  - [два `Ok(())` подряд](/home/pg/monorepo/trustme/tests/rust_doctest/upstream/std/io/mod__L2379_runtime.rs:19);
  - [потерянный hidden setup](/home/pg/monorepo/trustme/tests/rust_doctest/upstream/core/sync/atomic__L3500_50.rs:6).
- Уже найдено минимум 15 очевидно испорченных файлов; реальный масштаб определяется reference-прогоном.
- Разобрать hang `coretests/net`: подтверждён exit 124, блокирует 47 library tests.
- Убрать compiler ICE/assert:
  - `hir_from_ast_expr.cpp:26` — пустой AST expression;
  - `hir_from_ast.cpp:297` — invalid pattern;
  - `hir_typeck_expr_cs.cpp:8416` — spare rules;
  - `hir_typeck_expr_cs.cpp:1704` — оставшийся infer;
  - MIR/const-eval asserts и TODO.

### P1 — максимальная отдача на одну починку

Поддержать `#[should_panic = "message"]`.

Сейчас [expand_test.cpp](/home/pg/monorepo/trustme/rustc/expand_test.cpp:66) при любом payload безусловно ожидает `(`, поэтому name-value форма падает на `=`.

Одна эта починка разблокирует пять крупных harness’ов и 875 leaf tests:

- `coretests/iter`: 262;
- `std/sync`: 253;
- `coretests/slice`: 151;
- `alloctests/vec`: 148;
- `alloctests/rc`: 61.

Следующие harness blockers:

- `<$T>::MAX...` внутри macro expansion: 147;
- rest-pattern lowering `ref mut sub @ ..`: 107;
- macro parsing сложных attribute/token значений: 72;
- полноценный `offset_of!`: 55, текущий TODO в [parse_expr.cpp](/home/pg/monorepo/trustme/rustc/parse_expr.cpp:1419).

Всего 40 failed harness’ов блокируют 1 772 library leaf tests. Первые десять объясняют 1 303 из них.

### P2 — miscompile/runtime correctness

Это важнее добавления очередной принимаемой синтаксической формы:

- неправильный размер enum discriminant: `2` вместо `1`;
- неправильные `f128` результаты;
- неправильное форматирование отрицательных чисел;
- half-open range pattern semantics;
- потерянный `track_caller` filename;
- ошибки integer intrinsics и signedness;
- ABI/layout/packed/drop проблемы.

Хорошая стартовая выборка: 265 broken plain-stable `run-pass` и 116 library leaf tests, у которых harness уже собирается.

### P3 — type system и основная Rust-семантика

- dyn-trait canonicalization и auto-trait dedup;
- HRTB/lifetimes;
- associated types;
- projection/normalization;
- generic inference и const generics;
- pattern usefulness.

Здесь основной сигнал — `rust_ui_compile`: сломаны 261 из 288 шардов. Сначала чинить stable-конструкции; feature-gated `run-pass` сейчас имеет 237 failures из 460.

### P4 — compiler modes и CLI

- Реализовать настоящий check-only/metadata stop после typecheck для 2 506 `check-pass`.
- Из 175 flag-зависимых `run-pass` сломаны 152.
- Сначала безопасные diagnostic-only флаги вроде `--check-cfg`.
- Затем корректно отображать семантические `-C/-Z`; молча игнорировать `overflow-checks`, `panic`, target features и подобные нельзя.

### P5 — отдельные платформенные режимы

После основного Rust:

- gccrs `no_core`: 300/301 failures, в основном отсутствующие language items;
- RustSmith: 10/10 broken shards;
- оставшиеся vendor/book/reference shards.

Практический порядок первых работ: doctest validation → `should_panic =` → hang/ICE → rest patterns/`offset_of!`/macro-qualified paths → runtime miscompiles → trait/type system → check-only и CLI.
