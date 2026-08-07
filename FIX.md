Приоритет я бы поставил так — по severity и числу разблокируемых тестов.

### P0 — сначала убрать ложные сигналы и зависания

- ~~Исправить doctest extractor и проверять каждый результат эталонным Rust 1.90 перед импортом.~~ Сделано: fence больше не пересекает границу непрерывного doc-comment блока, qualified `Result::Ok(())` не дублируется, каждый кандидат компилируется и запускается точным `rustc 1.90.0` с проверкой ожидаемого exit mode. Из 3 807 кандидатов приняты 3 492.
  - ложный `std/io/mod__L1613_runtime.rs` с prose и вложенным Markdown удалён: это был closing fence предыдущего item, ошибочно принятый за новое начало;
  - `std/io/mod__L2379_runtime.rs` исправлен и проходит targeted runtime;
  - `core/sync/atomic__L3500_50.rs` честно восстановить статически нельзя: setup создаётся `#[doc = concat!(...)]` при macro expansion. Теперь такой разорванный doc-блок исключается, а не импортируется без setup.
- ~~Уже найдено минимум 15 очевидно испорченных файлов; реальный масштаб определяется reference-прогоном.~~ Закрыто полным reference compile+runtime импорт-прогоном: 315 из 3 807 кандидатов отвергнуты до записи в corpus.
- ~~Разобрать hang `coretests/net`: подтверждён exit 124, блокирует 47 library tests.~~ Сделано: regression `test_typeck_large_macro.rs` до починки зависал в `Lower MIR` дольше 60s, после починки полный compile+runtime занимает около 8s. MIR value-state теперь хранится один раз на basic block и распространяется FIFO-worklist без path/state-копий; packed merge работает по байтам. Type inference заранее отбрасывает bounds, не содержащие целевой ivar. Квадратичные `UnifyBlocks` и `ReborrowOfUnused` заменены hash/indexed проходами, дублирующие защитные MIR-валидации убраны при сохранении проверки после реально меняющих MIR фаз. Монолитный `net` после этого проходил compiler pipeline, но один 15-МБ generated-C translation unit всё ещё не укладывался вместе с внешним clang в общий 60s timeout, поэтому импорт разбит по upstream-модулям и compile-shards; самый тяжёлый shard собирается за 41.41s, все 47/47 runtime leaf tests проходят.
- Убрать compiler ICE/assert:
  - ~~`hir_from_ast_expr.cpp:26` — пустой AST expression;~~ Сделано: value-less `yield;` теперь lowering-ится в pool-allocated unit expression. Красный `test_yield_unit.rs` фиксирует compile+runtime, исходный `coroutine/borrow-in-tail-expr.rs` проходит. `coroutine/addassign-yield.rs` больше не падает здесь и доходит до отдельной MIR validity ошибки (`_6` non-valid), которая остаётся в MIR/const-eval пункте ниже;
  - ~~`hir_from_ast.cpp:297` — invalid pattern;~~ Сделано: slice parser теперь распознаёт `ref mut tail @ ..` как mutable-reference rest binding вместо range с двумя `Invalid` endpoints. Красный `test_slice_rest_ref_mut.rs` проходит compile+runtime, как и исходный `borrowck-slice-pattern-element-loan-rpass.rs`. Вариант на массиве фиксированной длины теперь проходит HIR и вскрывает отдельный MIR cast assert, оставленный в MIR-пункте ниже;
  - ~~`hir_typeck_expr_cs.cpp:8416` — spare rules;~~ Закрыто как ложный сигнал старого doctest extractor: оба trigger’а (`alloc/sync.rs:237` и `std/panic.rs:111`) содержали сгенерированный `Result<(), impl Debug> { Ok(()) }`, который эталонный Rust 1.90 сам отклоняет с E0282. После reference-validated reimport этих inputs в corpus нет;
  - ~~`hir_typeck_expr_cs.cpp:1704` — оставшийся infer;~~ Сделано: const generic matcher сохраняет более конкретное значение при повторном совпадении параметра, а выбранный exact trait impl теперь добавляет свои `where`-bounds так же, как единственный fuzzy impl. Красный `test_const_generic_method_inference.rs` и четыре `as_chunks`/`as_rchunks` doctest проходят compile+runtime; `{Infer(0)}` корректно связывается с `N = 2`;
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
- ~~rest-pattern lowering `ref mut sub @ ..`: 107;~~ HIR blocker снят вместе с `hir_from_ast.cpp:297`; fixed-array варианты упираются в отдельный MIR cast assert;
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
