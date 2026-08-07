Приоритет я бы поставил так — по severity и числу разблокируемых тестов.

### P0 — сначала убрать ложные сигналы и зависания

- ~~Исправить doctest extractor и проверять каждый результат эталонным Rust 1.90 перед импортом.~~ Сделано: fence больше не пересекает границу непрерывного doc-comment блока, qualified `Result::Ok(())` не дублируется, каждый кандидат компилируется и запускается точным `rustc 1.90.0` с проверкой ожидаемого exit mode. Из 3 807 кандидатов приняты 3 492.
  - ложный `std/io/mod__L1613_runtime.rs` с prose и вложенным Markdown удалён: это был closing fence предыдущего item, ошибочно принятый за новое начало;
  - `std/io/mod__L2379_runtime.rs` исправлен и проходит targeted runtime;
  - `core/sync/atomic__L3500_50.rs` честно восстановить статически нельзя: setup создаётся `#[doc = concat!(...)]` при macro expansion. Теперь такой разорванный doc-блок исключается, а не импортируется без setup.
- ~~Уже найдено минимум 15 очевидно испорченных файлов; реальный масштаб определяется reference-прогоном.~~ Закрыто полным reference compile+runtime импорт-прогоном: 315 из 3 807 кандидатов отвергнуты до записи в corpus.
- ~~Разобрать hang `coretests/net`: подтверждён exit 124, блокирует 47 library tests.~~ Сделано: regression `test_typeck_large_macro.rs` до починки зависал в `Lower MIR` дольше 60s, после починки полный compile+runtime занимает около 8s. MIR value-state теперь хранится один раз на basic block и распространяется FIFO-worklist без path/state-копий; packed merge работает по байтам. Type inference заранее отбрасывает bounds, не содержащие целевой ivar. Квадратичные `UnifyBlocks` и `ReborrowOfUnused` заменены hash/indexed проходами, дублирующие защитные MIR-валидации убраны при сохранении проверки после реально меняющих MIR фаз. Монолитный `net` после этого проходил compiler pipeline, но один 15-МБ generated-C translation unit всё ещё не укладывался вместе с внешним clang в общий 60s timeout, поэтому импорт разбит по upstream-модулям и compile-shards; самый тяжёлый shard собирается за 41.41s, все 47/47 runtime leaf tests проходят.
- Убрать compiler ICE/assert:
  - ~~`hir_from_ast_expr.cpp:26` — пустой AST expression;~~ Сделано: value-less `yield;` теперь lowering-ится в pool-allocated unit expression. Красный `test_yield_unit.rs` фиксирует compile+runtime, исходный `coroutine/borrow-in-tail-expr.rs` проходит;
  - ~~`hir_from_ast.cpp:297` — invalid pattern;~~ Сделано: slice parser теперь распознаёт `ref mut tail @ ..` как mutable-reference rest binding вместо range с двумя `Invalid` endpoints. Красный `test_slice_rest_ref_mut.rs` проходит compile+runtime, как и исходный `borrowck-slice-pattern-element-loan-rpass.rs`. Вариант на массиве фиксированной длины теперь проходит HIR и вскрывает отдельный MIR cast assert, оставленный в MIR-пункте ниже;
  - ~~`hir_typeck_expr_cs.cpp:8416` — spare rules;~~ Закрыто как ложный сигнал старого doctest extractor: оба trigger’а (`alloc/sync.rs:237` и `std/panic.rs:111`) содержали сгенерированный `Result<(), impl Debug> { Ok(()) }`, который эталонный Rust 1.90 сам отклоняет с E0282. После reference-validated reimport этих inputs в corpus нет;
  - ~~`hir_typeck_expr_cs.cpp:1704` — оставшийся infer;~~ Сделано: const generic matcher сохраняет более конкретное значение при повторном совпадении параметра, а выбранный exact trait impl теперь добавляет свои `where`-bounds так же, как единственный fuzzy impl. Красный `test_const_generic_method_inference.rs` и четыре `as_chunks`/`as_rchunks` doctest проходят compile+runtime; `{Infer(0)}` корректно связывается с `N = 2`;
  - MIR/const-eval asserts и TODO.
    - ~~fixed-array `ref mut tail @ ..` строил недопустимый cast `&mut T` сразу в `*mut [T; N]`;~~ Сделано: lowering берёт raw pointer на первый элемент rest-подмассива и выполняет корректный thin pointer-to-pointer cast. Красный `test_array_rest_ref_mut.rs` и upstream `borrowck-closures-slice-patterns-ok.rs` проходят compile+runtime.
    - ~~`coroutine/addassign-yield.rs` использовал non-valid `_6` после resume;~~ Сделано: call arguments остаются активными до завершения вычисления всего списка аргументов, поэтому временный `&mut String` сохраняется в coroutine state, если следующий аргумент делает `yield`. Красный `test_coroutine_addassign_yield.rs`, исходный тест и его drop-tracking вариант проходят compile+runtime.
    - ~~const-eval не умел читать `f16`, а внутренний `F16` неверно кодировал exponent/subnormal;~~ Сделано: 16-битное чтение симметрично записи, binary16↔binary32 conversion корректно обрабатывает normal, subnormal, infinity, NaN и round-to-nearest-even. Красный `test_f16_const_next_down.rs` проверяет локальный литерал и `f16::MIN_POSITIVE.next_down() == 0x03ff`; `libstd` полностью пересобран новым компилятором, `coretests/floats` доходит до следующего отдельного TODO на float remainder.
    - ~~const-eval не реализовывал float remainder;~~ Сделано через `std::fmod`, сохраняющий Rust/IEEE знак остатка. Красный `test_const_float_remainder.rs` проверяет `10.0 % 2.0 == 0.0` и `-5.5 % 2.0 == -1.5`; `coretests/floats` доходит до следующего отдельного TODO на intrinsic `fabsf64`.
    - ~~const-eval не реализовывал `fabsf{16,32,64,128}`;~~ Сделано точным побитовым сбросом sign bit без потери NaN payload и `f128` precision. Красный `test_const_float_abs.rs` проверяет `abs()` и `is_finite()`; `coretests/floats` доходит до следующей отдельной panic-ветки в constant evaluation.
    - ~~float arithmetic наследовала host-dependent знак NaN (`0.0 / 0.0` давало `0xfff8_0000_0000_0000`);~~ Сделано: MIR const propagation нормализует арифметический NaN в positive preferred NaN, а interpreter пишет canonical NaN отдельно для `f16/f32/f64/f128`. Красный `test_const_nan_sign.rs` проверяет и локальное деление, и `f64::NAN` из заново собранной `libstd`; `coretests/floats` проходит этот assert и доходит до следующей отдельной panic-ветки в constant evaluation.
    - ~~`SwitchValue` в const interpreter читал `ti.bits / 8` как количество бит и сравнивал `U128` с `uint64_t` через обрезающий до 32 бит overload;~~ Сделано: чтение использует полную битовую ширину, switch-литерал явно расширяется до `U128`. Красный `test_const_switch_u64.rs` проверяет высокое `u64`-значение; `coretests/floats` полностью проходит `nan::const_` и доходит до отдельного сбоя `f128::MIN_POSITIVE.is_normal()`.
    - ~~float literals и MIR constants хранились в `double`, поэтому `f128::MIN_POSITIVE` обнулялся ещё при чтении metadata;~~ Сделано: 16-байтный inline `FloatValue` на `_Float128` проведён через lexer, AST, HIR, MIR и metadata, а `F128` преобразуется побитово без потери precision. Красный `test_f128_const_is_normal.rs` проверяет точные биты `1u128 << 112` и `is_normal()`; `coretests/floats` проходит исходный assert и доходит до следующего отдельного TODO на intrinsic `minnumf64`.
    - ~~const evaluator не реализовывал intrinsic-операции `minnum`/`maxnum`;~~ Сделано для `f16/f32/f64/f128` с IEEE number-семантикой, включая NaN с любой стороны и signed zero; C backend для поддержанных `f32/f64` переведён с неверного ternary на `fmin`/`fmax`. Красный `test_const_float_min_max.rs` проверяет const для всех четырёх типов и runtime для `f32/f64`; `coretests/floats` проходит всё семейство и доходит до следующего отдельного TODO на `copysignf64`.
    - ~~const evaluator не реализовывал `copysignf{16,32,64,128}`;~~ Сделано побитовым переносом только sign bit через `U128`, без преобразования float и потери NaN payload. Красный `test_const_float_copysign.rs` проверяет точные payload/sign bits всех четырёх типов и runtime `f32/f64`; `coretests/floats` проходит `copysign` и доходит до следующего отдельного TODO на `floorf64`.

### P1 — максимальная отдача на одну починку

~~Поддержать `#[should_panic = "message"]`.~~ Сделано: test expansion различает name-value `= "message"` и list `expected = "message"`, а harness больше не выкидывает все `should_panic` tests. Красный `test_should_panic_name_value.rs` запускает настоящий `--test` harness и показывает `1 passed`; upstream `lazy::reentrant_init` также появился в listing и проходит runtime. Пять крупных harness’ов с 875 leaf tests больше не блокируются на этом атрибуте, хотя у части остаются следующие независимые ошибки: `alloctests/rc` доходит до внешнего clang и падает на `DynMetadata` ABI, `coretests/slice` — до `Resolve Use`.

Затронутые harness’ы:

- `coretests/iter`: 262;
- `std/sync`: 253;
- `coretests/slice`: 151;
- `alloctests/vec`: 148;
- `alloctests/rc`: 61.

Следующие harness blockers:

- ~~`<$T>::MAX...` внутри macro expansion: 147;~~ Сделано: range parser теперь считает `<` и `<<` допустимым началом правой границы, поэтому `0..<$T>::MAX.count_ones()` после macro substitution не обрывается как пустой range. Красный `test_macro_qualified_type_path.rs` проходит compile+runtime; `coretests/num` проходит весь Expand и доходит до отдельной ошибки `Resolve Use` в `flt2dec/random.rs`;
- ~~rest-pattern lowering `ref mut sub @ ..`: 107;~~ HIR и fixed-array MIR blockers сняты; slice/array regression units и оба ближайших upstream borrowck-теста проходят compile+runtime;
- ~~macro parsing сложных attribute/token значений: 72;~~ Сделано для точного blocker-а: cfg evaluator теперь понимает boolean predicates `cfg(true)`/`cfg(false)`, в том числе вложенный `cfg(not(false))`, после macro substitution. Красный `test_cfg_bool_macro_attribute.rs` проходит compile+runtime; `coretests/floats` проходит Expand и Resolve Use и доходит до отдельного macro-path сбоя `crate::assert_biteq_rt`;
- ~~macro re-export alias из внешнего модуля терял содержащий module path;~~ Сделано: fast path одночастного relative macro lookup заполняет canonical path текущим модулем, поэтому alias из внешнего parent и повторный import во внешнем child больше не превращают `crate::parent::original` в `crate::original`. Красный двухуровневый `test_macro_rules_reexport_alias.rs` проходит compile+runtime; `coretests/floats` доходит до отдельного const-eval сбоя на `f16`.
- ~~массовое форматирование изменило control flow старых `if (...) case ...` трюков;~~ Сделано: diff `8dbe2c1d^..8dbe2c1d` проверен по исходным токенам; все шесть опасных `if → case → compound` мест сведены к трём switch-блокам и переписаны явными case/ветками в trait/impl modifiers, block-expression continuation и comparison dispatch. Повторный поиск не находит такой формы в текущем дереве; красные `test_trait_unsafe_method.rs` и `test_block_expression_method.rs` проходят compile+runtime, новый `libstd` полностью собирается.
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
