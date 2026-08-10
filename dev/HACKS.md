Аудит ведётся строго по `bin/rustc/**`. Ниже — актуальное состояние после удаления compatibility-кода до Rust 1.90 и literal-false веток.

Найдено:

- 134 строки с `hack/HACK/hacky/hackery/hackiness` в 31 файле.
- 126 точных употреблений слова `HACK`.
- 128 комментариев и 6 диагностических `DEBUG`-строк.
- По подсистемам: frontend — 42, HIR/typeck — 57, MIR — 16, backend — 17, инфраструктура — 2.

Главный вывод: массово удалять эти комментарии нельзя. Под одной меткой смешаны реальные ошибки модели, допустимые lowerings, устаревший код и просто плохо названные инварианты.

### Самые опасные места

1. Macro hygiene фактически неполна.

   [ident.cpp](/home/pg/monorepo/trustme/bin/rustc/ident.cpp:9) реализует только примитивную видимость контекстов, а `Ident::operator==` вообще сравнивает только имя. [synext_macro.cpp](/home/pg/monorepo/trustme/bin/rustc/synext_macro.cpp:2094) вырезает hygiene из строкового представления. Hardcoded bypass `tracing_attributes::{instrument}` удалён: proc-macro больше не отбрасывается по имени crate, а `unit_proc_macro_attribute` проверяет его фактический вызов. Общий долг hygiene при этом остаётся.

2. HRTB/lifetime relation местами математически некорректна.

   В [hir_path.cpp](/home/pg/monorepo/trustme/bin/rustc/hir_path.cpp:326) `TraitPath::ord` считает два пути равными, если хотя бы у одного есть `#apply_elision`. Такое равенство нетранзитивно и нарушает требования `std::map/std::set`.

   Тот же кластер включает sentinel `#apply_elision`, синтетический `Self: Trait`, выбор единственного lifetime trait object, passthrough отсутствующих lifetime-параметров и замену lifetime в impl-методах. Это напрямую совпадает с HRTB/lifetime-elision группами из `FIX.md`.

3. Trait solver использует глобальное «не знаю — значит fuzzy».

   [hir_type.cpp](/home/pg/monorepo/trustme/bin/rustc/hir_type.cpp:1475) превращает несовпадение тегов в `Fuzzy` для opaque/unbound/placeholder; [hir_hir.cpp](/home/pg/monorepo/trustme/bin/rustc/hir_hir.cpp:697) считает unbounded infer подходящим любому impl; [hir_typeck_static.cpp](/home/pg/monorepo/trustme/bin/rustc/hir_typeck_static.cpp:1241) автоматически принимает bound для `_`.

   Это может не только задерживать решение, но и оставлять неверные impl-кандидаты. Правильная модель upstream — отдельный goal с результатом `Yes/Ambiguous/No`, а не ослабление отношения типов.

4. Coercion/unsize проходит через заведомо невалидный HIR.

   [hir_typeck_expr_cs.cpp](/home/pg/monorepo/trustme/bin/rustc/hir_typeck_expr_cs.cpp:4243) создаёт invalid `_Unsize`, который позднее чинит [hir_expand_main_bindings.cpp](/home/pg/monorepo/trustme/bin/rustc/hir_expand_main_bindings.cpp:8690). Generic-array metadata отдельно угадывается в MIR.

   Общая relation `Pointee::Metadata` для struct tail уже реализована рекурсивно в обоих solver path и закрыла прежний compile blocker на 34 library cases. Невалидный `_Unsize` и отдельное угадывание generic-array metadata в MIR остаются: adjustment должен быть полностью типизирован до MIR.

5. Drop/move/match содержат реальные семантические обходы.

   - CTFE игнорирует все `Drop`: [hir_conv_constant_evaluation.cpp](/home/pg/monorepo/trustme/bin/rustc/hir_conv_constant_evaluation.cpp:2198).
   - Grouped match сначала полностью flatten’ится: [mir_from_hir.cpp](/home/pg/monorepo/trustme/bin/rustc/mir_from_hir.cpp:6225).
   - Move из `Box` распознаётся по физическому шаблону полей.
   - MIR validator помечает значение валидным просто потому, что встретил `Drop`.
   - DCE удаляет drop неиспользуемого local с незакрытым вопросом о conditional drop.

   Это подтверждает текущие P2-группы drop/unwind и or-pattern runtime.

6. Backend местами генерирует программу, которая просто вызывает `abort()`.

   [trans_codegen_c.cpp](/home/pg/monorepo/trustme/bin/rustc/trans_codegen_c.cpp:2893) заменяет большой класс MSVC AVX-функций на `abort`, причём условие содержит `true ||`. Аналогично `vmov/vexpand/vpexpand` для GCC-like backend.

   Там же workaround GCC bug определяется по компилятору, которым собран trustme, а не по C-компилятору, запускаемому backend. При clang-сборке trustme и runtime `CC=gcc` workaround не включится.

7. Mangling допускает потенциальные коллизии.

   [trans_mangling.cpp](/home/pg/monorepo/trustme/bin/rustc/trans_mangling.cpp:254) кодирует значения associated types trait object, но не их имена, предполагая одинаковый набор. `-` и `#` также сознательно сводятся к одному представлению.

### Статус доказанно мёртвого кода

- [x] Две ветки `range_full` под `!TARGETVER_LEAST_1_54` удалены вместе со всем механизмом `TARGETVER_*`.
- [x] Блок non-lang operator paths под `TARGETVER_MOST_1_19` удалён.
- [x] Удалены все 58 блоков `#if 0`, обычные `if (false)` и связанные с ними мёртвые HACK-комментарии. `unit_compiler_no_dead_branches` запрещает возвращать literal-false ветки; debug-макросы сохраняют compile-time проверку выражений через unevaluated `sizeof`.
- [x] Обходы `tracing_attributes-0_1_26` и `tracing_attributes-0_1_30` удалены. Настоящий `tracing-attributes 0.1.30` и workload из 236 `#[instrument]` собираются; отдельный semantic unit запрещает снова тихо отбрасывать attribute proc-macro.

`rustc_legacy_const_generics` и `builtin # offset_of` удалять только из-за старого комментария нельзя: соответствующая семантика всё ещё может присутствовать в исходниках, компилируемых Rust 1.90. Optional leading `|` в match arm подтверждён как штатная грамматика и теперь так и документирован в parser.

### Полный реестр по файлам

| Подсистема | Файлы и строки | Вывод |
|---|---|---|
| Driver | `main_bindings.cpp:1868,2241` | Debugger pause, CLI parsing и emulated `-vV` документированы как штатные driver features. Глобальная настройка crate loader и отключённый повтор lifetime validation остаются долгом. |
| Expansion | `expand_common.cpp:25,137,387,460,949,2092,2223,2431` | Глобальный module context, повторные проходы и early `macro_rules`; hardcoded потеря `tracing`-семантики удалена. |
| Parser | `parse_common.cpp:263,306,354,388,403,1279,1353` | Штатные token splitting, `Fn(...)`, optional leading `|`, tuple-field visibility и внутренние path encodings больше не помечены как HACK. Оставшиеся маркеры относятся к statement/path macro handling, `TOK_HASH` между statements и `builtin #` lowering. |
| Macro matcher | `macro_rules_macro_rules.cpp:534,833,1537,2311,2313,3811` | Реальные opaque-fragment и matcher-state обходы; строка 534 привязана к ICU из rustc 1.90. |
| Hygiene/macros | `ident.cpp:9`, `synext_macro.cpp:1559,2094` | Неполная hygiene и hardcoded `format_args!` API. |
| Resolver | `resolve_common.cpp:98,543`; `resolve_main_bindings.cpp:411,641,1512,1702,1760,1851,1860,2681,3792,3858,3891,4096` | Синтетические `=crate`, anon-module и primitive-module paths — единый долг модели путей. |
| Decorators | `synext_decorator.cpp:1555,2475,2612,2972` | Повторный обход anon modules и частный workaround для `windows-0.48`. |
| HIR layout | `hir_expr.h:580` | У `ExprNode_Emplace::Noop` не найден producer, но consumers ещё существуют. Это кандидат на доказанное удаление мёртвого HIR, а не ложный marker. |
| HIR lowering | `hir_from_ast.cpp:176,562,589,590,1620,3041,3631` | Self/HRTB sentinels, синтетический trait bound, null HIR pointer как discriminator. |
| HIR conversion | `hir_conv_main_bindings.cpp:657,949,1875,2372,2381,2724,3244,4144,4356` | Lifetime heuristics, `#` в enum path, approximate DST и privacy bypass `fmt::rt::Argument`. |
| HIR expansion | `hir_expand_main_bindings.cpp:114,4553,6867,8690` | Literal-false experiments удалены; active invalid-unsize, generic-array metadata и placeholder fallback остаются. |
| HIR identity | `hir_hir.cpp:203,697`; `hir_path.cpp:326`; `hir_type.cpp:1475,1481,1490` | Const ordering, нетранзитивный HRTB order и fuzzy type relation — высокий риск. |
| Metadata | `hir_main_bindings.cpp:1075` | Контракт basename + `.hir` теперь документирован как соглашение custom metadata; empty crate-name rewrite остаётся compatibility debt. |
| Typeck common | `hir_typeck_common.cpp:503,515,753,761` | Erasure и passthrough lifetime вместо явных binders. |
| Typeck solver | `hir_typeck_expr_cs.cpp:932,2030,4243,4935,5806,5820,6591,7443,7903,8047,8227`; header `:175` | Остались active heuristics и DEBUG-текст; operator result=LHS и arbitrary ivar fallback требуют отдельных units. |
| Typeck helpers | `hir_typeck_helpers.cpp:6025,7183` | Opaque fuzzy matching и array→slice shortcut вместо нормального receiver adjustment. |
| Typeck impls | `hir_typeck_main_bindings.cpp:2121,2349,2383` | Lifetime bounds копируются/заменяются для совпадения представления. |
| Static solver | `hir_typeck_static.cpp:478,508,1241,2085` | Associated bounds дописываются, `_` автоматически проходит bound, opaque equality обходится локально. |
| Const eval | `hir_conv_constant_evaluation.cpp:1326,2198,3448,3463` | One-past-end допустим; ignore Drop и «roughly-correct» monomorph state — реальные пробелы. |
| MIR lowering | `mir_from_hir.cpp:203,1109,1594,1621,6225,6258,8888`; header `:41` | Drop flags, unsize metadata, match flattening и hardcoded Box move. |
| MIR validation | `mir_helpers.cpp:626` | Validator скрывает отсутствие корректного validity analysis. |
| MIR passes | `mir_operations.cpp:2804,3630,3943,4011,4037,4748,9847` | Target-width constants, complete-type include, conservative intrinsic-wrapper inlining и drop-flag compaction документированы как invariants. Остались Box layout coupling, generic metadata, usage downgrade и опасное drop removal. |
| C backend | `trans_codegen_c.cpp:1280,1291,1903,2089,2526,2887,2893,3154,5291,5930` | CAS helpers, `.rlib`/object protocol, `const_eval_select` call adapter и overflow intrinsics документированы как backend conventions. Остались принудительный `-O1`, platform workarounds, incomplete asm translation и runtime `abort`. |
| Enumeration | `trans_main_bindings.cpp:1349,2204,2487,2872` | Generated statics, `caller_location`, default trait bodies и lifetime population обходят неполную dependency model. |
| Mangling | `trans_mangling.cpp:70,72,254` | Потенциальные symbol collisions. |

Следующая последовательность: ложные markers в parser/lexer, infrastructure, driver, общих HIR definitions, metadata, безопасных MIR invariants и backend conventions переименованы. Общая `Pointee::Metadata` relation и layout always-unsized struct закрыты; следующий функциональный пункт опасного пункта 4 — убрать invalid `_Unsize` и MIR-угадывание metadata, строго unit-first. Macro hygiene остаётся отдельным архитектурным пунктом, но больше не оправдывает тихое удаление proc-macro.
