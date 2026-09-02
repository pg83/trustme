# HACKS.md — инвентаризация хаков компилятора

Полная ревизия 2026-09-02 на HEAD `dddb7b5be`. Скоуп — `bin/rustc/**`.
Считались маркеры (`TODO`, `HACK`, `BUG:`, «for now», «roughly», «quirk»)
и немаркированные структурные обходы. Строки уплывут — перегрепать; имена
и формулировки стабильнее строк.

## Сводные числа (база для будущего ratchet)

- `TODO` всего: 1039, из них **исполняемые бомбы** `TODO(...)`/`MIR_TODO(...)`
  (недореализованный случай = падение компилятора на входе): **291**.
- `HACK`-маркеры: **60**.
- `const_cast` (мутация «иммутабельных» структур): **69**
  (hir_conv_constant_evaluation 16, mir_operations 8, mir_from_hir 6,
  hir_expand 5, expand_proc_macro 4, прочие ≤3).
- Прочие маркеры (`BUG:` в сообщениях, «for now», «roughly», «quirk»): 11.
- `abort()` вне генерируемого кода: только fatal-пути `compile_error.cpp`
  и обработка ошибок `hir_serialise_lowlevel.cpp` (+1 в
  `hir_main_bindings.cpp:1075` рядом с empty-crate-name хаком).

## Класс А. Семантические хаки — компилятор может молча принять/породить неверное

Самый опасный класс; каждый пункт — отдельная цель «починить механизмом».

Типы и трейты:
- `hir_type.cpp:2013,2019,2028` — структурный компаратор превращает
  Opaque/Unbound/placeholder в fuzzy-совпадение. После солвер-кампаний
  влияет только на index-префильтр и `HIRMatchGenerics`-матчеры, но
  остаётся ослаблением отношения на нижнем слое.
- `hir_hir.cpp:961` — impl-индекс: «unbounded infer подходит любому impl»
  (префильтр; страхуется транзакционной унификацией, но сам допуск —
  наследие).
- `hir_typeck_static.cpp:821` — «shouldn't need this, works around some
  missing cases» в static resolve.
- `hir_typeck_expr_cs.h:62` — операторы над примитивами: результат ВСЕГДА
  тип левого операнда (зашитое правило вместо вывода из impl).
- `hir_typeck_expr_cs.cpp:7277` — forwarding-impls обёрток могут давать
  неверные правила (признано в комментарии).
- `hir_from_ast.cpp:2294` — синтетический bound `Self: ThisTrait`
  («TODO: Remove this, it's evil»).
- `hir_conv_main_bindings.cpp:2995` — эвристика выбора управляющего
  `?Sized`-параметра для DST.

Приватность/язык:
- `hir_conv_main_bindings.cpp:3827` — обход приватности для
  `fmt::rt::Argument` (парный хак к зашитому lowering `format_args!` в
  `synext_macro.cpp` — builtin-макрос строит вызовы конкретного API).
- `parse_common.cpp:4888` — `impl const` не отражается полностью:
  valueless impl const переживает cfg без отказа; constness trait-impl
  границы неполна.
- `hir_typeck_expr_cs.cpp:7077` — принадлежность узла unsafe-блоку не
  вычисляется (проверки unsafe неполны).

Const eval / память:
- `hir_conv_constant_evaluation.cpp:3278` — «generate a roughly-correct
  one» (monomorph state).
- `hir_conv_constant_evaluation.cpp:4858` — допуск one-past-end указателя
  ради `[foo, ref bar @ ..]`.
- `hir_conv_constant_evaluation.cpp:883` — «this pointer will be
  invalidated…» (живое UB-подозрение в интерпретаторе).

MIR:
- `mir_operations.cpp:6530` — подавление borrow-extension ломается, если
  присваивание в цикле → возможны двойные move (признано).
- `mir_operations.cpp:1283` — downgrade Usage при move «covers some
  quirks».
- `mir_from_hir.cpp:8358` — генераторы: drop-флаги перемапливает вызывающий.
- `mir_from_hir.cpp:10045` — virtual unsize: «emit cast, пусть monomorph
  починит».
- `hir_expand_main_bindings.cpp:3226` — «self_ty here is wrong, the
  borrow needs to be included».
- `hir_expand_main_bindings.cpp:1458` — pre-pass, помечающий все closures
  как !Copy до инференса.
- `hir_expand_main_bindings.cpp:5059` — размер generic-массивов в обход
  `Target_GetSizeAndAlignOf`.
- `hir_expand_main_bindings.cpp:6714` — «probably good enough for now?».

Кодогенерация/линковка:
- `trans_mangling.cpp:248,249` — `-` и `#` кодируются одинаково;
  `:416` — «assume all TraitObject types have the same aty set». Оба —
  потенциальные коллизии символов.
- `trans_main_bindings.cpp:3944` — `caller_location` создаёт пустую
  локацию.
- `trans_main_bindings.cpp:641` — отказ эмитить unused generated statics
  (обход неполной dependency model).

## Класс Б. Исполняемые бомбы — 291 `TODO(...)`/`MIR_TODO(...)`

Недописанные случаи, роняющие компилятор на легальном входе. Горячие
файлы: mir_from_hir 57 (паттерн-матчинг по struct/union/array/borrow,
overlap-проверки), hir_conv_constant_evaluation 32 (арифметика
float/signed/unsigned, EnumValue/StructConstant), expand_proc_macro 22,
trans_codegen_c 20, resolve_main_bindings 16 (trait alias, кросс-крейт
пути), hir_from_ast 14, synext_decorator 13, parse_common 12 (включая
`offset_of` tuple-индексы), trans_main_bindings 10, hir_typeck_expr_cs
10, mir_operations 9 (virtual call через Pin, hidden vtable),
macro_rules 9. Направление: каждый случай — либо реализация, либо
честный `ERROR` с диагностикой; `TODO(...)`-макрос в финале не существует.

## Класс В. Архитектурные обходы

- Двойные проходы и глобальный контекст expansion:
  `expand_common.cpp:2378` (Expand_Mod дважды), `:1796` (macro_rules в
  первый проход, «1.74 HACK»), `:1421` (inner items → outer), `:1925`
  (пустое имя крейта → builtins); дубль-обходы anon-модулей в
  `synext_decorator.cpp:1442,1532`.
- Резолвер: «EVIL HACK» anon-модули указывают на родителя
  (`resolve_main_bindings.cpp:4411,4492`), `=`-префикс имени крейта как
  маркер 2018-абсолютного пути (`:1345,:4532`), мутация исходника для
  auto trait (`:3212`), `Self` сквозь type alias (`:6424`), примитив как
  модуль (`:1179`), рекурсивный фикс путей импортов (`:973`).
- Парсер statement-макросов: `parse_common.cpp:3651,3690,3752`
  (включая совместимость с rust#78829); `macro_rules`: точечный
  workaround под icu_locid_transform_data (`macro_rules_macro_rules.cpp:363`),
  «force no match?» (`:1339`), EOF-хвост entry (`:2740`),
  TOK_INTERPOLATED_TYPE (`:525`).
- `HIRExprNodeEmplace::Noop` (`hir_expr.h:397`) — no-op узел «to allow
  coercion»; producer не найден, consumers живы.
- `hir_main_bindings.cpp:811` — пустое имя прочитанного крейта молча
  заменяется на имя загрузки (+ `abort()` рядом).
- `hir_path.cpp:956` — сортировка по const-param как tie-breaker.
- 69 `const_cast` — мутации интернированных/иммутабельных структур в
  обход владения; треть — в const eval.

## Класс Г. Backend-компромиссы

- `trans_codegen_c.cpp:1198` — принудительный `-O1` (обход GCC
  miscompilation); `:1209` — отключение стадии оптимизации под GCC bug
  117423. Оба выбираются по компилятору, СОБРАВШЕМУ trustme, а не по
  C++-компилятору, которым backend реально компилирует — неверный
  детектор.
- `trans_codegen_c.cpp:5756` — генерируемая программа содержит `abort()`
  вместо `v*`-операций (vmov/vexpand и т.п.) — рантайм-бомба в готовом
  бинаре.
- `trans_codegen_c.cpp:1930` — NonZero-оптимизированные enum как struct
  с одним полем; `:1666` nested unsized; неполная трансляция asm/AVX.
- `-fno-strict-aliasing` не выставляется при собственном `-fwrapv` —
  подозрение, связанное с той же «GCC miscompilation» (см. -O1).

## Класс Д. Триаж-вопросы (~95)

`TODO: What/Why/Is this…` — сомнения автора, не диагнозы
(`mir_operations.cpp:1288` «Is this right?», `:707`, `:854`,
`trans_codegen_c.cpp:1227,1516`, `resolve_main_bindings.cpp:4845`
name collisions, `macro_rules:2716` collision и т.д.). Каждый при
изживании либо повышается в класс А/Б с фиксом, либо закрывается
доказательством корректности и удалением маркера.

## Класс Е. Перф/косметика

`TODO: Cache/Optimise/Use TU_MATCH/Convert to Revisitor/Print sorted…` —
не влияют на семантику; чинятся попутно, отдельной кампании не требуют.

## Плотность по файлам (TODO+HACK; ratchet-база)

mir_from_hir 133 · mir_operations 87 · hir_typeck_expr_cs 72 ·
resolve_main_bindings 70 · trans_codegen_c 62 · synext_decorator 60 ·
hir_conv_main_bindings 56 · hir_conv_constant_evaluation 50 ·
parse_common 49 · hir_from_ast 48 · expand_proc_macro 35 ·
macro_rules_macro_rules 34 · expand_common 30 · hir_expand 29 ·
trans_main_bindings 28 · hir_hir 19 · hir_typeck_main_bindings 18 ·
mir_helpers 17 · hir_typeck_helpers 17 · hir_typeck_static 15 ·
hir_main_bindings 15 · trans_target 14 · synext_macro 12 ·
resolve_common 12 · main_bindings 11 · остальные ≤8.

## Порядок изживания (набросок кампании)

1. Класс А по подсистемам (семантика; каждый пункт = правило/механизм,
   не заплатка), начиная с mangling-коллизий и MIR-семантики (двойные
   move) — они портят готовые программы молча.
2. Класс Г — детектор GCC-бага по фактическому C++-компилятору,
   расследование miscompilation (aliasing?), снятие `-O1`; `abort()`-вставки
   → реализация или честный отказ на этапе компиляции.
3. Класс Б — покрыть бомбы корпусом (какие достижимы на Rust 1.90) и
   реализовать; остальные → диагностика. `TODO(...)`-макрос удалить.
4. Класс В — архитектурные: expansion-проходы, anon-модули, `=`-пути,
   `Emplace::Noop`, `const_cast`-зачистка.
5. Класс Д триаж → пополняет А/Б либо удаляется.
6. Маркерный ratchet по числам выше — вниз до нуля.
