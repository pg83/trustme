# План исправлений

В этом файле остаётся только незакрытая функциональная работа. История исправлений находится в git.

## Gate и приоритизация

```sh
nix --extra-experimental-features 'nix-command flakes' develop .#clang -c env CC=clang CXX=clang++ LDFLAGS='-fuse-ld=lld' ./build -B .build-clang -j "$(nproc)" -k test
```

Приоритет определяется не краснотой отдельного unit и не размером каталога, а числом targets, которые снимает одно общее исправление. Одинаковый текст верхнеуровневой ошибки (`Unexpected token`, `Type mismatch`, `Failed to find an impl`) сам по себе не доказывает общую причину. Если минимизация показывает разные причины, группа делится и части реклассифицируются ниже.

Для compiler fix порядок строгий: минимальный `tst/unit/test_*.rs`, который сразу зелёный на системном Rust 1.90 и красный на `mrustc` → исправление общего пути → тот же unit остаётся зелёным на системном Rust 1.90 и становится зелёным на `mrustc` → исходный upstream trigger → соседние triggers той же сигнатуры → clang/lld build → commit и push. Промежуточные баги также фиксируются отдельными unit и сначала проверяются системным Rust 1.90. Тесты не подгоняются под текущее поведение компилятора.

## Измеренный baseline

Полный fast gate на `d8f40501`, 2026-08-12, `-j 78 -k`: **14 469 targets, 12 572 зелёных, 1 897 красных**.

| Корпус | Красных |
|---|---:|
| `rust_ui_compile` | 856 |
| `rust_1_90` | 460 |
| `rust_lib` | 197 |
| `rust_reference` | 125 |
| `rust_doctest` | 124 |
| `unit` | 31 |
| `miri` | 30 |
| `gccrs_compile` | 29 |
| `gccrs` | 13 |
| `nomicon` | 13 |
| `rustsmith` | 8 |
| `rust_book` | 3 |
| `rust_quiz` | 3 |
| `exercism_rust` | 2 |
| `rust_by_example`, `rustlings`, `async_book` | 1 каждый |

По способу завершения: 1 304 `exit 250` (обычно `mrustc` завершился через `SIGABRT` после diagnostic/assert), 434 `exit 1`, 89 runtime/test `exit 101`, 53 прямых `SIGSEGV`, 12 timeout, 3 `SIGILL`, 2 ошибки декодирования adapter. Ещё два `SIGSEGV` скрыты обёрткой `unit/run_one.py`, поэтому фактический crash inventory — **55**.

В 513 красных targets первым внутренним маркером является явный assert/TODO из `bin/rustc`; это 125 разных `file.cpp:line`. Первые десять сигнатур дают 254 targets, первые двадцать — 315. Остальные большие классы (`Unexpected token` — 348, `Type mismatch` — 93, `Failed to find an impl` — 65) пока являются только симптомами и не считаются одним исправлением.

## P1 — 25–99 targets одним общим исправлением

1. [ ] **Conditional const bounds `[const]`: 28 targets.** Parser одинаково видит `TOK_SQUARE_OPEN, expected TOK_IDENT` в const-trait bounds. Реализовать синтаксис, HIR-представление и trait-selection semantics; простого принятия токенов недостаточно.

## P2 — 10–24 targets одним общим исправлением

1. [ ] **Inherent associated types: 24 targets.** Одна сигнатура `hir_from_ast.cpp:2365`, `Unexpected item type in inherent impl - Type`. После HIR lowering проверить lookup, normalization, generics и visibility, а не только убрать assert.

2. [ ] **Macro matcher: 20 targets.** Одна сигнатура `macro_rules_macro_rules.cpp:2113`, `Macro_InvokeRules_MatchPattern - No arm matched`, проходит через UI, Rust 1.90 и Reference tests. Минимизировать по одной matcher state transition; отдельно проверить interpolated block/type/visibility/meta fragments и statement boundaries.

3. [ ] **`-Cdebuginfo`: 16 targets.** Связать уровни с backend output либо доказанно классифицировать tests, которым нужен только принятый driver contract. Не принимать option без эффекта там, где тест проверяет debug info.

4. [ ] **CTFE `simd_extract`: 16 targets.** Одна сигнатура `hir_conv_constant_evaluation.cpp:3408`; покрывает Rust 1.90, library, Miri и doctest. Реализовать bounds/type/layout semantics и unit для valid и out-of-bounds lane.

5. [ ] **Delegation `reuse`: 13 прямых parser failures.** Реализовать современный синтаксис delegation вместе с HIR/resolution, включая glob/list/rename/override и impl-trait cases.

6. [ ] **CTFE null relocation: 11 targets.** `hir_conv_constant_evaluation.cpp:1571`, `Null (<PTR_BASE) pointer deref`. Искать место потери relocation/provenance, а не ослаблять assert.

7. [ ] **Const pattern literal borrow: 11 targets.** `mir_from_hir.cpp:4443`, `append_from_lit Match literal Borrow`. Добавить корректное MIR lowering и проверить custom equality/branch selection.

## P3 — измеренные, но раздробленные группы

### Crash inventory

- [ ] **53 upstream `SIGSEGV`: сначала stacks, затем реклассификация.** 26 прямых UI, 24 Rust 1.90, 2 doctest и 1 GCCRS. Снять серии stack traces по фазам и объединять только одинаковые причины. Не считать crashes одним compiler fix.

### Driver options

Всего **224 прямых отказа**: 134 неизвестных `-Z`, 84 неизвестных `-C`, 5 прочих driver options и 1 asm option. Это не одно исправление.

- [ ] MIR control: `validate-mir` — 22, `mir-enable-passes` — 9, `inline-mir`/`inline_mir` — 8, `lint-mir` — 5. Связать с validator/pass selection; принятие без изменения pipeline не считается реализацией.
- [ ] Codegen: `codegen-units` — 9, `link-dead-code` — 6, `debug_assertions` — 5, `no-prepopulate-passes` — 5, `overflow-checks=on` — 4, `target-feature` — 3, `lto` — 3, затем единичные LTO/mangling/coverage/link options. Группировать только по общему backend behavior.
- [ ] Driver/front-end: `crate-attr` — 8, `verbose-internals` — 5, `print-type-sizes` — 5, `parse-crate-root-only` — 4 и оставшиеся `--print`, `--explain`, `--env-set`/`-Z` options. Diagnostic-only tests должны действительно проверять output, а не зеленеть от игнорирования флага.
- [ ] `-Z unpretty` требует разных printer/stop-point реализаций: `expanded` — 8, `hir` — 7, `normal` — 4, `thir-tree` — 4, `hir,typed` — 2, `thir-flat` — 2, по одному `expanded,hygiene`, `hir-tree` и `mir-cfg`. Принятие option без соответствующего вывода не считается исправлением.

### Parser, resolver и type system

- [ ] После вынесенных выше `[const]` и `reuse` остаётся 281 `Unexpected token` target. Крупнейшие наблюдаемые формы — `fn` вместо `{` (включая семь `coretests/pin_macro`, остановившихся на вложенном `async fn`), lifetime/function modifiers, async closures, range/or-pattern syntax и interpolated macro fragments. Каждую форму минимизировать до grammar/expansion причины до назначения приоритета.
- [ ] 94 `Type mismatch`, 66 `Failed to find an impl`, 23 `Unknown macro` и 17 `No applicable methods` сначала кластеризовать по semantic path. Trait objects, HRTB/binders, projections, const inference, TAIT/RPITIT и async lowering не объединять по тексту diagnostic.
- [ ] Полный source-scoped lint store отсутствует: кроме CLI-level `unexpected_cfgs`, `allow/warn/deny/forbid/force-warn` пока не производят rustc diagnostics, а positive harness их не сравнивает. Добавить diagnostic-verifying nodes и измерить fan-out.
- [ ] Настоящий check-only driver (`--emit=metadata`/stop after typeck) остаётся отдельной задачей. Перед повышением приоритета заново измерить текущие красные `check-pass`; `build-pass` обязан проходить полный pipeline.

### CTFE, MIR и const generics

- [ ] Остальные повторяющиеся явные сигнатуры: primitive operator против `Add::Output` — 9, unsized MIR local — 8, expanded generic `Infer` в CTFE — 8, erased `sizeof` — 7, higher-ranked lifetime assert — 6, spare typecheck rules — 6, CTFE slice out of range — 6, generic `Deref` path в trans — 2. Для каждой нужен unit на минимальный trigger до исправления.
- [ ] Реализовать оставшиеся CTFE float `signum`, `three_way_compare`, `black_box` с relocations и invalid enum tag; не пропускать `f128` через host `double`.
- [ ] Не возвращать path-copy алгоритмы и не ослаблять MIR validation ради зелёного теста.

### Runtime correctness

97 targets компилируются, но завершаются с `exit 101`: Rust 1.90 — 55, library — 35, Miri — 3, Reference — 3, GCCRS — 1. Это не одна задача.

- [ ] В library-группе отдельно кластеризовать float formatting (4), numeric formatting (2), time formatting (2), `NonZero` bit operations (5), `std::error` multiline formatting (6), panic `Location` (4), `i128/u128` carrying arithmetic (2), saturating integer arithmetic (2), waker identity (2), C string formatting (2) и одиночные `MaybeUninit` formatting, `type_name`, `Any` и const string pointer cases.
- [ ] Для остальных runtime failures сравнить полученное значение/bits/ABI, а не только panic text. Отдельно держать async-drop ordering (8), i128/u128, float/NaN/signed zero, derived traits, `track_caller`, `TypeId`/`Any`, process environment, SIMD и aggregate move/drop.
- [ ] Добавить metadata encoding для cross-crate enum discriminants шире 64 бит и проверить producer/consumer crates.

## P4 — crash/timeout и unit-регрессии после крупных групп

- [ ] Три `SIGILL` остаются в `const-generics/issues/issue-74906.rs`, `layout/invalid-unsized-const-prop.rs`, `const_prop/issue-86351.rs`.
- [ ] 12 timeout: три Rust Book, три UI, три RustSmith, по одному Rust 1.90 (`for-loop-while/label_break_value.rs`), doctest и Exercism. Для каждого снять stack/phase; лимит не увеличивать.
- [ ] Медленная integration-группа (`resvg` и вынесенные тяжёлые upstream library tests) не входит в этот fast baseline. После серии fast fixes прогонять её отдельно; `resvg` по-прежнему требует минимального solver unit для `AsRef<Option<HuffmanTable>>`.

Compile-fail tests, единственный ожидаемый эффект которых требует полноценного borrow checker, не должны вытеснять compile-pass, runtime и crash clusters. После каждого общего fix пересчитать fan-out затронутой группы; полный gate запускать после серии fixes на всех доступных ядрах.
