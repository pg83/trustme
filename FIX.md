# План исправлений

В этом файле остаётся только незакрытая работа. История уже сделанных исправлений находится в git, а не в плане.

Текущий baseline полного plain-clang/lld прогона: 11 658 failed nodes, 14 009 broken requested targets. Команда: `nix --extra-experimental-features 'nix-command flakes' develop .#clang -c env CC=clang CXX=clang++ LDFLAGS='-fuse-ld=lld' ./build -B .build-clang -j 4 -k test`. Полный лог сохранён в `/tmp/trustme-full-gate-20260808-zst-codegen.log`. Падения `libcore` в `ptr::alignment`, `ptr::const_ptr`, never-type impl и `partial_ord_impl!(f16 ...)`, typecheck `alloc::boxed::Deref` и move из `Box::into_inner` устранены; точный Rust 1.90 standard-library cargo graph теперь полностью проходит compile и C codegen.

Для каждого compiler fix порядок один: минимальный красный `tests/unit/test_*.rs` → исправление общего пути → зелёный unit → точный upstream trigger → сборка `rustc` под clang/lld → отдельный commit и push. Полный gate запускается после серии точечных исправлений, а не после каждого файла.

## P1 — ICE, assert и зависания

- [ ] Разобрать timeout `coretests/net_ipv6_properties`: определить, 60 секунд съедает rustc, внешний clang или runtime. Compiler hang свести к unit; слишком крупный независимый harness разбить на compile shards без изменения тестового содержания.
- [ ] Разобрать все timeout RustSmith shards. Сначала определить конкретный input внутри каждого shard; одинаковые compiler hangs объединить по stack/phase, каждый новый общий hang фиксировать одним минимальным unit.

## P2 — runtime miscompile и ABI

- [ ] Разделить `i128/u128` runtime failures по операции: arithmetic, comparison, cast, shift и ABI передачи/возврата. На каждую реально отличающуюся причина — маленький unit с точным ожидаемым значением; сначала чинить причину, разблокирующую больше upstream-тестов.
- [ ] Исправить float runtime methods, где фактический результат `0` вместо `1`. Отделить ошибку C codegen от libstd/CTFE, проверить signed zero, NaN и оба `f32/f64` там, где используется общий emitter.
- [ ] Исправить drop ordering. Unit должен записывать порядок destructor calls в массив/счётчик и покрывать normal scope exit, early return и partial initialization; не завязывать решение на borrow checker.
- [ ] Исправить runtime для derived `Copy`/`Clone`, не смешивая derive expansion с последующим move/drop lowering. Проверить aggregate с scalar и nested aggregate fields.
- [ ] Исправить process environment runtime failures: минимально проверить set/get/remove и различить ошибку платформенного `libstd` от ABI/codegen ошибки компилятора.
- [ ] Реализовать runtime hidden-caller ABI для `Location::caller()` и проверить filename/line через `#[track_caller]`; const-вариант не считать покрытием runtime-варианта.
- [ ] Довести runtime `f128` arithmetic/comparison/casts, которые сейчас уходят в `abort()`. Проверять точные binary128 bits, не пропускать значения через host `double`.
- [ ] Добавить metadata encoding для cross-crate enum discriminants шире 64 бит и проверить отдельными producer/consumer crates.
- [ ] Исправить оставшиеся formatting miscompile: exponent precision и debug-hex parser. На каждый формат — точная строка как runtime invariant.
- [ ] Разобрать `packed-struct-drop-aligned.rs`: сначала починить `Pin<&mut generator>.resume`, затем проверить фактические layout/drop invariants исходного теста.

## P3 — CTFE и MIR semantics

- [ ] Исправить `Encountered Infer value in constant` после подстановки вложенных const expressions. Сначала один минимальный красный unit из общей части `interior-with-const-generic-expr.rs`, `infer-too-generic.rs` и `nested_uneval_unification-1.rs`; определить, где concrete outer argument превращается в `Infer`, и сохранить его до CTFE вместо подстановки фиктивного значения.
- [ ] Устранить рекурсивный Typecheck/CTFE вход в `subexprs_are_const_evalutable.rs`, который заканчивается assert `debug.cpp:49: g_debug_indent_level < MAX_INDENT_LEVEL`. Сначала минимальный red unit на `N * 2` внутри generic `foo`; по backtrace определить цикл между const evaluation и type expansion. После фикса выражение должно оставаться параметризованным при проверке `foo`, вычисляться после `foo::<10>` и дать runtime-длину `21`.
- [ ] Исправить `Handle expanded generic: Infer(0)` в `nested_uneval_unification-2.rs`. Unit должен отдельно покрыть передачу identity const argument `{{ L }}` через две generic функции и получить массив длины `2`; перед исправлением проверить, является ли это тем же местом потери аргумента, что и предыдущий пункт, и объединить fixes только при общей причине.
- [ ] Реализовать float `signum` в CTFE: текущий путь ожидает integer, но получает `Float`. Проверить `±1`, `±0`, infinity и NaN для поддержанных float widths.
- [ ] Исправить rotate intrinsics и их проверку count: нормализовать count по ширине типа и покрыть `0`, `BITS`, `BITS + 1` и signed operand.
- [ ] Реализовать/исправить CTFE `simd_extract`, сохранив lane type и bounds; invalid lane должен диагностироваться, valid lane — давать точное значение.
- [ ] Реализовать CTFE `three_way_compare` с корректной signedness и шириной операндов.
- [ ] Реализовать CTFE `black_box` как identity с сохранением значения/relocations, не как потерю evaluation state.
- [ ] Исправить invalid enum tag `255`: определить layout, из которого CTFE читает неверный discriminant, и проверить valid/invalid niche cases отдельно.
- [ ] Проверить MIR value propagation на новых failures после P0: не возвращать path-copy алгоритмы и не ослаблять validation ради прохождения теста.

## P4 — stable parser, expansion и resolver

- [ ] `rust_quiz/020-break-return-in-condition.rs`: исправить разбор неоднозначного `if break { ... }`. После устранения MIR `No value available` тест компилируется, но печатает `1212` вместо upstream `121`; различить grammar для `return`/`break` с block expression и формы в скобках.
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

## P5 — type system

- [ ] Canonicalization trait objects: стабилизировать principal/supertrait lookup. Начать с failures, которые не требуют borrow checker.
- [ ] HRTB и двухслойные binder substitutions: минимизировать каждый failure до конкретного misplaced/de-Bruijn lifetime.
- [ ] Associated types и projection normalization: сначала concrete equality-bound cases, затем generic projections; не подменять unresolved projection первым найденным impl.
- [ ] Generic inference и const generics: группировать по месту потери constraints, отдельно exact impl selection и fallback.
- [ ] Pattern usefulness/exhaustiveness: сначала runtime-correct arm selection, затем diagnostics compile-fail.

## P6 — compiler modes и CLI

- [ ] Реализовать настоящий check-only/metadata stop после typecheck для `check-pass`; такой тест считается зелёным без C generation/link/runtime.
- [ ] Принимать и применять diagnostic-only options `--check-cfg`, `-A` и `-D`, чтобы они не завершали компилятор как unknown option.
- [ ] Реализовать требуемые output/debug modes вроде `-Z unpretty`; не игнорировать их молча, если тест проверяет output.
- [ ] Реализовать управляемые MIR passes для `-Z mir-enable-passes`, `-Z inline-mir`/`inline_mir` и `-Z validate-mir`; каждый флаг должен менять соответствующий pass/validation pipeline, а не только приниматься CLI.
- [ ] Семантически поддержать используемые `-C overflow-checks`, `-C panic`, target features и остальные codegen flags. Игнорирование допустимо только после доказательства, что flag не влияет на проверяемый invariant.

## P7 — отдельные платформенные наборы

- [ ] RustSmith: после устранения timeout из P1 разделить настоящие compile/runtime miscompile по общей compiler signature.
- [ ] Остальные vendor/book/reference shards чинить после stable run-pass и library tests, кроме случаев, когда один небольшой общий fix разблокирует сразу несколько семейств.

Не брать в ближайшую очередь compile-fail tests, единственный ожидаемый эффект которых требует полноценного borrow checker. Они остаются красными до отдельного проекта по borrow checking и не должны вытеснять ICE, runtime correctness и stable compile+run.
