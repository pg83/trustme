# План исправлений

В этом файле остаётся только незакрытая функциональная работа. История исправлений находится в git.

## Текущий baseline

Полный gate на commit `22eccad8` запускался на всех доступных ядрах:

```sh
nix --extra-experimental-features 'nix-command flakes' develop .#clang -c env CC=clang CXX=clang++ LDFLAGS='-fuse-ld=lld' ./build -B .build-clang -j "$(nproc)" -k test
```

Результат: `3289 failed nodes`, `3260 broken requested targets`. Лог: `/tmp/trustme-full-gate-20260809-all-cores.log`.

Из 2384 целых записей о прямом падении: 1571 compiler abort/rejection, 627 обычных compile/adapter failures, 97 runtime failures, 75 SIGSEGV, 11 timeout и 3 SIGILL. Ещё две строки были повреждены параллельным выводом. Основные прямые источники: `rust_ui_compile` — 963, `rust_1_90` — 534, GCCRS — 412, `rust_lib` — 149, reference и doctest — по 127.

Приоритет определяется не краснотой отдельного unit и не размером каталога, а числом targets, которые снимает одно общее исправление. Число рядом с задачей — измеренный fan-out этого gate. Если минимизация показывает разные причины, задача делится, а части реклассифицируются ниже.

Для compiler fix порядок остаётся строгим: минимальный красный `tst/unit/test_*.rs` → исправление общего пути → зелёный unit → исходный upstream trigger → соседние triggers той же сигнатуры → clang/lld build → commit и push. Unit подтверждает причину, но сам по себе не повышает приоритет.

## P1 — 25–99 targets одним общим исправлением

1. [x] **`-C opt-level` и `debug-assertions`: устранены 65 прямых отказов.** `opt-level=0/1/2/3/s/z` связан с MIR-inlining и флагами C backend, `-O` и `-C opt-level` подчиняются правостороннему precedence rustc, а обе формы `debug-assertions` управляют встроенным cfg с rustc-default от optimization level. Unit проверяет cfg, MIR, backend command и invalid values; исходные triggers больше не останавливаются на этих опциях.

2. [x] **Offset pointer в const borrow: устранены 62 library failures.** MIR `ItemAddr` теперь хранит allocation path вместе с byte offset; MIR cleanup, monomorphization, CTFE, metadata и C backend сохраняют его без потерь. Unit покрывает tuple field, `Option`/`Result` payload и cross-crate generic MIR через сериализацию; все 33 `coretests/option` и 29 `coretests/result` зелёные.

3. [ ] **Оставшиеся `-C` options: 55 прямых отказов.** Разделить `debuginfo`, `codegen-units`, target features, LTO и прочие по фактической семантике; опция считается реализованной только когда меняет соответствующий pipeline/backend behavior.

4. [ ] **Настоящий check-only: 49 измеренных failures.** Из 815 текущих красных `check-pass` только 49 проходят `-Z stop-after=typeck`; это реальный, а не верхний fan-out. Реализовать `--emit=metadata`/check stop в driver и включать его в adapter только для `check-pass`; `build-pass` обязан оставаться на полном pipeline.

5. [ ] **Оставшиеся `no_core` lang-item paths: 43 прямых отказа.** `coerce_unsized` — 37, `unsafe_cell` — 5 и `tuple_trait` — 1. Для каждого сначала сверить upstream semantics и отделить настоящий lang-free builtin от неполного GCCRS fixture; отсутствие trait нельзя обходить, если upstream требует trait для самой операции.

6. [ ] **MIR control flags: 37 прямых отказов.** Связать `-Z validate-mir` — 20 с validator pipeline, `mir-enable-passes` — 9 и `inline-mir`/`inline_mir` — 8 с реальным pass selection. Не считать зелёным простое принятие option.

7. [x] **`Pointee`/metadata solver: устранён compile blocker для 34 library cases.** Оба solver path теперь вычисляют `Metadata` рекурсивно через фактическое последнее поле struct, включая вложенный generic tail и `dyn`; unit проверяет равенство `<Wrapper<T> as Pointee>::Metadata = <T as Pointee>::Metadata`, а исходный UI trigger `pointee-tail-is-generic.rs` и compile-фаза `coretests/ptr` проходят этот blocker. Runtime layout/codegen дефекты отделены ниже.

8. [ ] **`pin!` expansion/parser: не менее 28 targets.** 21 compile failure видит `let` после path separator, ещё 7 cases заблокированы harness `coretests/pin_macro`. Исправить statement macro expansion в block context, затем проверить `pin!` с expression, `let` и function item.

9. [ ] **Повторяющиеся compiler crash signatures.** Сначала символизировать и группировать 75 SIGSEGV по stack/phase. Уже видны TAIT/impl-trait, coroutine/generator drop, projection cycles, const generics и HRTB; повышать отдельную группу выше можно только с измеренным общим fan-out. Отдельно устранить 28 `Invalid path (no nodes)` asserts, 26 `Spare rules left after typecheck stabilised` и 24 `Unexpected item type in inherent impl - Type`.

## P2 — runtime correctness и общие codegen/CTFE причины

97 targets компилируются, но падают при исполнении. Их нельзя объединять в одну задачу: порядок внутри раздела пересчитывается по числу triggers одной минимальной причины.

- [ ] `i128/u128`: разделить arithmetic, comparison, cast, shift и ABI передачи/возврата; начинать с общей операции, встречающейся в максимуме runtime failures.
- [ ] Drop/unwind/leak: wildcard discard в `let _ = value` и `_ = value` теперь сохраняет drop, два runtime target зелёны. Общий unit должен записывать destructor order и дополнительно покрывать normal exit, early return, partial initialization и panic path. Не ослаблять тесты утечек.
- [ ] Or-pattern/match lowering: guard-failure теперь продолжает со следующей альтернативы того же arm, а multiple or-pattern перебираются left-to-right; три runtime target зелёны. Осталось исправить grouped lowering для unguarded nested slice/struct patterns и закрыть оставшиеся runtime arm-selection triggers.
- [ ] Float runtime и formatting: отдельно signed zero/NaN, arithmetic, exponent precision и debug-hex; ожидаемая строка или bits являются invariant.
- [ ] Derived `Copy`/`Clone`/`Debug`/`Hash`: отделить ошибку expansion от move/drop/codegen aggregate.
- [ ] `track_caller`, `type_name`/`TypeId`/`Any`, process environment, SIMD и nonzero arithmetic: группировать только по общему lowered ABI или intrinsic.
- [ ] Довести `f128` runtime без пропускания binary128 через host `double`; проверить точные bits.
- [ ] Добавить metadata encoding для cross-crate enum discriminants шире 64 бит и проверить producer/consumer crates.
- [x] Always-unsized struct layout совпадает с layout его sized stand-in до хвостового поля. Rust-layout теперь стабильно сортирует поля по убывающей effective alignment group и исключает DST tail; `caller_location` не зависит от физического порядка полей. Unit `test_always_unsized_struct_raw_parts.rs`, соседние packed/offset units и upstream `unsized3-rpass.rs` зелёные после полной пересборки libstd.
- [x] Array→slice autoderef больше не создаёт невалидное unsized value в HIR. Method/index paths получают явную borrow → pointer unsize → deref adjustment chain; отдельный kind ограничивает позднюю смену place mutability этой цепочкой. Unit покрывает shared/unique slice methods и range indexing, полный libstd rebuild и шесть соседних array/slice nodes зелёные.
- [x] Bounded generic `A: Unsize<[T]>` больше не читает `DstMeta` из thin `&A`. MIR сохраняет unresolved unsize до monomorphization, затем общий cleanup получает длину concrete array; красный unit `&[u8; 4] → &[u8]`, полный libstd rebuild и связанный adjustment unit зелёные.
- [ ] `packed-struct-drop-aligned.rs`: сначала исправить `Pin<&mut generator>.resume`, затем layout/drop invariant.

## P3 — оставшиеся CTFE, MIR и const generics

- [ ] Объединить только после доказательства общей причины: `Encountered Infer value in constant`, потерю outer argument в nested unevaluated const, identity const argument `{{ L }}` и рекурсивный вход Typecheck/CTFE для `N * 2`.
- [ ] Обычное pointer equality интернированных типов не заменять semantic comparison. Два красных unit на unevaluated const, различающихся lifetime metadata (`'M0` против `'#omitted`), должны использовать явную const relation.
- [ ] Реализовать CTFE float `signum`, rotate normalization, `simd_extract`, `three_way_compare`, `black_box` с relocations и корректную обработку invalid enum tag `255`.
- [ ] Исправить 12 `Null (<PTR_BASE) pointer deref` и 7 `sizeof on an erased type` по месту потери relocation/type, не по месту assert.
- [ ] Не возвращать path-copy алгоритмы и не ослаблять MIR validation ради зелёного теста.

## P4 — parser, macros, resolver и type system без доказанного крупного fan-out

- [ ] Полный source-scoped lint store отсутствует: кроме CLI-level `unexpected_cfgs`, `allow/warn/deny/forbid/force-warn` пока не производят rustc diagnostics, а текущий positive harness их не сравнивает. Сначала добавить diagnostic-verifying nodes и измерить fan-out; не считать простую успешную компиляцию lint UI семантическим покрытием.
- [ ] Macro matcher: сгруппировать `No arm matched` по одной matcher state transition; отдельно interpolated block/type/visibility/meta fragments и statement boundaries.
- [ ] Associated inherent types: после общего HIR lowering fix для 24 текущих asserts проверить lookup/normalization и visibility, а не просто принимать item.
- [ ] Trait objects, HRTB/binders, associated projections, generic/const inference и TAIT: минимизировать до конкретного misplaced binder, потерянного constraint или normalization cycle. Не подменять unresolved projection первым impl.
- [ ] Stable syntax: function header modifiers, raw identifiers 2024, unsafe attributes, postfix match, nested or-patterns, range precedence и `if break {}`.
- [ ] Resolver: re-export/use chains, `CoercePointee`, async fn/async trait lowering и builtin macros `concat_bytes`, `offset_of`, `type_ascribe`.
- [ ] Pattern usefulness/exhaustiveness: сначала runtime-correct arm selection, затем diagnostics compile-fail.

## P5 — единичные regressions и независимые failures

- [ ] Семь красных unit — это три причины, а не семь приоритетов: два lifetime-elision SIGSEGV на yield/coroutine, два const-relation mismatch и три empty-path asserts в Trans Enumerate. Они закрываются вместе с соответствующим общим кластером выше.
- [ ] Три независимых library CTFE panic: `cell::refcell_borrow`, `cell::refcell_borrow_mut` и `mem::test_transmute_copy`. Старый monolithic fan-out 94 не подтверждён: при раздельной сборке остальные 91 нода либо зелёные, либо относятся к cfg-selection/runtime failure. Каждый panic сначала минимизировать до неверного branch или CTFE значения.
- [ ] `resvg`: `AsRef` selection для `Option<HuffmanTable>`; после минимального trait-solver unit вернуть весь standing integration в gate.
- [ ] Три SIGILL: `const-generics/issues/issue-74906.rs`, `layout/invalid-unsized-const-prop.rs`, `const_prop/issue-86351.rs`.
- [ ] Оставшиеся timeout после `coretests/iter`: два UI, три Rust 1.90, один Exercism, три RustSmith и один runtime `select_nth_unstable`. Каждый сначала привязать к stack/phase; лимит не увеличивать.
- [ ] Reference, doctest, book и vendor failures поднимать выше только когда одна минимальная причина подтверждённо снимает больше targets, чем текущий P0/P1.

Compile-fail tests, единственный ожидаемый эффект которых требует полноценного borrow checker, не должны вытеснять перечисленные compile-pass, runtime и crash clusters. После каждого общего fix пересчитать fan-out затронутой группы; полный gate запускать после серии fixes на всех доступных ядрах.
