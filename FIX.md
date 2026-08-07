# План исправлений

В этом файле остаётся только незакрытая работа. История уже сделанных исправлений находится в git, а не в плане.

Текущий baseline полного plain-clang/lld прогона: граф из 9 942 узлов, 2 916 failed nodes, 2 881 broken requested targets. Команда: `nix develop .#clang -c ./build -B .build-clang -k test`.

Для каждого compiler fix порядок один: минимальный красный `tests/unit/test_*.rs` → исправление общего пути → зелёный unit → точный upstream trigger → сборка `rustc` под clang/lld → отдельный commit и push. Полный gate запускается после серии точечных исправлений, а не после каждого файла.

## P0 — ICE, assert и зависания

Исполнять строго сверху вниз.

- [x] ~~Исправить `raw-ref-op/raw-ref-op.rs`: сравнение `*const T` с `&T` приводит к assert в `HIR::TypeData::as_Borrow()`.~~ Исправлено: `MIR_Optimise_DeTemporary_Borrows` ошибочно читал borrow kind через тип local, хотя raw borrow создаёт local типа `Pointer`; теперь уже существующий `RValue::Borrow` является единым источником `BorrowType` и для reference, и для raw pointer. Красный `test_raw_pointer_reference_eq.rs` воспроизводил `SIGABRT` и теперь проверяет `*const T == &T`, `*const T == *mut T` и dereference compile+runtime; точный upstream `raw-ref-op.rs` зелёный. Новых аллокаций и копий типов нет.
- [ ] Исправить `traits/inheritance/repeated-supertrait.rs`: method lookup теряет `same_as` у trait object с повторяющимся supertrait. Unit должен оставить только минимальный diamond/repeated-supertrait и динамический вызов; затем прогнать исходный stable `run-pass`.
- [ ] Устранить семейство `hir_typeck_common.cpp:686` (`Value param ... out of range`). Сначала извлечь первый stable trigger из полного gate, зафиксировать конкретную generic substitution в unit и проверить отсутствие выхода за длину параметров, а не маскировать assert.
- [ ] Устранить assert `visit_expr hit in OuterVisitor` на static-borrow/constant inputs. Unit должен содержать минимальный const/static initializer, который сейчас попадает в expression visitor после ожидаемой фазы.
- [ ] Устранить trait-alias assert в `hir_from_ast.cpp:963`. Сначала минимальный alias с теми bounds, на которых падает lowering; после фикса проверить исходный UI/run-pass trigger.
- [ ] Устранить `mir_mir_builder.cpp:217: No value available`. Разделить triggers по конструкции, найти первый общий случай отсутствующего result value и не подставлять фиктивный unit для выражений с не-unit типом.
- [ ] Исправить slice-pattern lowering в `mir_from_hir_match.cpp:1944` (`too many leading rules`) и отдельный overlap byte-array patterns. Нужны units на leading/rest/trailing patterns и runtime-проверка выбранной match arm.
- [ ] Исправить validation `ItemAddr` для inline const. Unit должен вычислять адрес допустимого inline-const item и доходить до runtime без ослабления общей MIR validation.
- [ ] Разобрать timeout `coretests/net_ipv6_properties`: определить, 60 секунд съедает rustc, внешний clang или runtime. Compiler hang свести к unit; слишком крупный независимый harness разбить на compile shards без изменения тестового содержания.
- [ ] Разобрать все timeout RustSmith shards. Сначала определить конкретный input внутри каждого shard; одинаковые compiler hangs объединить по stack/phase, каждый новый общий hang фиксировать одним минимальным unit.

## P1 — runtime miscompile и ABI

- [ ] Разделить `i128/u128` runtime failures по операции: arithmetic, comparison, cast, shift и ABI передачи/возврата. На каждую реально отличающуюся причина — маленький unit с точным ожидаемым значением; сначала чинить причину, разблокирующую больше upstream-тестов.
- [ ] Исправить float runtime methods, где фактический результат `0` вместо `1`. Отделить ошибку C codegen от libstd/CTFE, проверить signed zero, NaN и оба `f32/f64` там, где используется общий emitter.
- [ ] Исправить drop ordering. Unit должен записывать порядок destructor calls в массив/счётчик и покрывать normal scope exit, early return и partial initialization; не завязывать решение на borrow checker.
- [ ] Исправить runtime для derived `Copy`/`Clone`, не смешивая derive expansion с последующим move/drop lowering. Проверить aggregate с scalar и nested aggregate fields.
- [ ] Исправить C backend для fat pointers и arrays: отдельно случаи undeclared `var0`, отсутствующего `.DATA` и неверной передачи metadata. Каждый C compile failure сначала фиксировать минимальным Rust unit, который воспроизводит конкретный испорченный C fragment.
- [ ] Исправить process environment runtime failures: минимально проверить set/get/remove и различить ошибку платформенного `libstd` от ABI/codegen ошибки компилятора.
- [ ] Реализовать runtime hidden-caller ABI для `Location::caller()` и проверить filename/line через `#[track_caller]`; const-вариант не считать покрытием runtime-варианта.
- [ ] Довести runtime `f128` arithmetic/comparison/casts, которые сейчас уходят в `abort()`. Проверять точные binary128 bits, не пропускать значения через host `double`.
- [ ] Добавить metadata encoding для cross-crate enum discriminants шире 64 бит и проверить отдельными producer/consumer crates.
- [ ] Исправить оставшиеся formatting miscompile: exponent precision и debug-hex parser. На каждый формат — точная строка как runtime invariant.
- [ ] Разобрать `packed-struct-drop-aligned.rs`: сначала починить `Pin<&mut generator>.resume`, затем проверить фактические layout/drop invariants исходного теста.

## P2 — CTFE и MIR semantics

- [ ] Реализовать float `signum` в CTFE: текущий путь ожидает integer, но получает `Float`. Проверить `±1`, `±0`, infinity и NaN для поддержанных float widths.
- [ ] Исправить rotate intrinsics и их проверку count: нормализовать count по ширине типа и покрыть `0`, `BITS`, `BITS + 1` и signed operand.
- [ ] Реализовать/исправить CTFE `simd_extract`, сохранив lane type и bounds; invalid lane должен диагностироваться, valid lane — давать точное значение.
- [ ] Реализовать CTFE `three_way_compare` с корректной signedness и шириной операндов.
- [ ] Реализовать CTFE `black_box` как identity с сохранением значения/relocations, не как потерю evaluation state.
- [ ] Исправить invalid enum tag `255`: определить layout, из которого CTFE читает неверный discriminant, и проверить valid/invalid niche cases отдельно.
- [ ] Проверить MIR value propagation на новых failures после P0: не возвращать path-copy алгоритмы и не ослаблять validation ради прохождения теста.

## P3 — stable parser, expansion и resolver

- [ ] `parser/fn-header-syntactic-pass.rs`: поддержать все допустимые комбинации `const`/`async`/`unsafe`/`extern` перед `fn`, сохранив rejection недопустимого порядка.
- [ ] `or-patterns/or-patterns-syntactic-pass.rs`: поддержать вложенные or-patterns в tuple/slice/struct contexts; runtime unit должен различать arms.
- [ ] `imports/issue-62767.rs`: исправить разрешение re-export/use chain `crate::bar::bar`, затем проверить соседние import tests того же shard.
- [ ] `liveness/liveness-upvars.rs`: перестать классифицировать обычную closure как generator только из-за захваченных переменных/синтаксиса тела.
- [ ] Поддержать raw identifiers Rust 2024 без смешения `r#name` с зарезервированным token.
- [ ] Исправить `pin!` macro input, где parser видит `let` после path separator; проверить statement macro expansion в block context.
- [ ] Поддержать unsafe attributes вида `#[unsafe(export_name = "...")]` и соседние `no_mangle`/`link_section` формы.
- [ ] Добавить derive handler `CoercePointee` либо корректный общий builtin-derive path; проверить generated impl, а не только принятие атрибута.
- [ ] Довести async fn/async trait parser/lowering triggers из stable corpus до HIR; generator/borrow-check diagnostics вне этой задачи.
- [ ] Исправить `nll/extra-unused-mut.rs` как type-inference failure, если минимизация подтвердит, что причина не требует NLL borrow checking.
- [ ] Исправить `nll/issue-54943-3.rs` (`!` против `()`) на уровне divergence/coercion, если trigger воспроизводится без borrow-check semantics.
- [ ] Исправить lifetime временного значения в `offset-of/offset-of-temporaries.rs`, не вводя полноценный borrow checker; сначала доказать минимальным unit, что это lowering/lifetime-elision bug.

## P4 — type system

- [ ] Canonicalization trait objects: deduplicate auto traits и стабилизировать principal/supertrait lookup. Начать с failures, которые не требуют borrow checker.
- [ ] HRTB и двухслойные binder substitutions: минимизировать каждый failure до конкретного misplaced/de-Bruijn lifetime.
- [ ] Associated types и projection normalization: сначала concrete equality-bound cases, затем generic projections; не подменять unresolved projection первым найденным impl.
- [ ] Generic inference и const generics: группировать по месту потери constraints, отдельно exact impl selection и fallback.
- [ ] Pattern usefulness/exhaustiveness: сначала runtime-correct arm selection, затем diagnostics compile-fail.

## P5 — compiler modes и CLI

- [ ] Реализовать настоящий check-only/metadata stop после typecheck для `check-pass`; такой тест считается зелёным без C generation/link/runtime.
- [ ] Принимать и применять diagnostic-only options `--check-cfg`, `-A` и `-D`, чтобы они не завершали компилятор как unknown option.
- [ ] Реализовать требуемые output/debug modes вроде `-Z unpretty`; не игнорировать их молча, если тест проверяет output.
- [ ] Семантически поддержать используемые `-C overflow-checks`, `-C panic`, target features и остальные codegen flags. Игнорирование допустимо только после доказательства, что flag не влияет на проверяемый invariant.
- [ ] Сохранить полный лог следующего batch gate и построить таблицу `family / phase / signature / exit code / affected nodes`, чтобы следующие приоритеты опирались на точные количества, а не на усечённый terminal stream.

## P6 — отдельные платформенные наборы

- [ ] gccrs `no_core`: сначала добавить минимальные обязательные language items (`sized`, затем реально запрошенные `sub` и остальные), после каждого изменения прогонять весь gccrs shard.
- [ ] RustSmith: после устранения timeout из P0 разделить настоящие compile/runtime miscompile по общей compiler signature.
- [ ] Остальные vendor/book/reference shards чинить после stable run-pass и library tests, кроме случаев, когда один небольшой общий fix разблокирует сразу несколько семейств.

Не брать в ближайшую очередь compile-fail tests, единственный ожидаемый эффект которых требует полноценного borrow checker. Они остаются красными до отдельного проекта по borrow checking и не должны вытеснять ICE, runtime correctness и stable compile+run.
