Аудит ведётся строго по `bin/rustc/**`. Ниже — актуальное состояние после удаления compatibility-кода до Rust 1.90 и literal-false веток.

Найдено:

- 170 строк с `hack/HACK/hacky/hackery/hackiness` в 36 файлах.
- 158 точных употреблений слова `HACK`.
- 163 комментария и 7 диагностических `DEBUG`-строк.
- По подсистемам: frontend — 56, HIR/typeck — 62, MIR — 21, backend — 23, инфраструктура — 8.

Главный вывод: массово удалять эти комментарии нельзя. Под одной меткой смешаны реальные ошибки модели, допустимые lowerings, устаревший код и просто плохо названные инварианты.

### Самые опасные места

1. Macro hygiene фактически неполна.

   [ident.cpp](/home/pg/monorepo/trustme/bin/rustc/ident.cpp:9) реализует только примитивную видимость контекстов, а `Ident::operator==` вообще сравнивает только имя. [synext_macro.cpp](/home/pg/monorepo/trustme/bin/rustc/synext_macro.cpp:2094) вырезает hygiene из строкового представления, а [expand_common.cpp](/home/pg/monorepo/trustme/bin/rustc/expand_common.cpp:233) из-за этого полностью игнорирует `tracing::instrument`. Это единый архитектурный дефект, а не три независимых костыля: легальная proc-macro семантика тихо исчезает.

2. HRTB/lifetime relation местами математически некорректна.

   В [hir_path.cpp](/home/pg/monorepo/trustme/bin/rustc/hir_path.cpp:326) `TraitPath::ord` считает два пути равными, если хотя бы у одного есть `#apply_elision`. Такое равенство нетранзитивно и нарушает требования `std::map/std::set`.

   Тот же кластер включает sentinel `#apply_elision`, синтетический `Self: Trait`, выбор единственного lifetime trait object, passthrough отсутствующих lifetime-параметров и замену lifetime в impl-методах. Это напрямую совпадает с HRTB/lifetime-elision группами из `FIX.md`.

3. Trait solver использует глобальное «не знаю — значит fuzzy».

   [hir_type.cpp](/home/pg/monorepo/trustme/bin/rustc/hir_type.cpp:1475) превращает несовпадение тегов в `Fuzzy` для opaque/unbound/placeholder; [hir_hir.cpp](/home/pg/monorepo/trustme/bin/rustc/hir_hir.cpp:697) считает unbounded infer подходящим любому impl; [hir_typeck_static.cpp](/home/pg/monorepo/trustme/bin/rustc/hir_typeck_static.cpp:1241) автоматически принимает bound для `_`.

   Это может не только задерживать решение, но и оставлять неверные impl-кандидаты. Правильная модель upstream — отдельный goal с результатом `Yes/Ambiguous/No`, а не ослабление отношения типов.

4. Coercion/unsize проходит через заведомо невалидный HIR.

   [hir_typeck_expr_cs.cpp](/home/pg/monorepo/trustme/bin/rustc/hir_typeck_expr_cs.cpp:4243) создаёт invalid `_Unsize`, который позднее чинит [hir_expand_main_bindings.cpp](/home/pg/monorepo/trustme/bin/rustc/hir_expand_main_bindings.cpp:8690). Generic-array metadata отдельно угадывается в MIR.

   Это тот же общий дефект, что `Pointee/metadata solver: 34 cases` в `FIX.md`: adjustment должен быть полностью типизирован до MIR, а metadata должна вычисляться общей relation.

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
- [ ] Обход `tracing_attributes-0_1_26` явно оставлен от rustc 1.74 и всё ещё исполняемо отбрасывает proc-macro по имени crate.

`rustc_legacy_const_generics`, `builtin # offset_of` и leading `|` в match удалять только из-за старого комментария нельзя: соответствующая семантика всё ещё может присутствовать в исходниках, компилируемых Rust 1.90.

### Полный реестр по файлам

| Подсистема | Файлы и строки | Вывод |
|---|---|---|
| Infrastructure | `common.h:17`, `tagged_union.h:150,177` | Сокращения `mv$` и macro-loop — плохая читаемость, но не ошибки семантики. |
| Driver | `main_bindings.cpp:1681,1868,2241,2481,2486` | Literal-false код удалён; testing/pipeline labels и ручной CLI parser остаются активной инфраструктурой. |
| Expansion | `expand_common.cpp:25,137,233,395,468,957,2100,2231,2439` | Глобальный module context, повторные проходы и early `macro_rules`; `tracing` — тихая потеря семантики. |
| Parser | `parse_common.cpp:263,306,354,388,403,555,939,1279,1353,1581,1664,1675,1678,1713,1807,2454,2963,3845,3851,4714` | Split `&&/<< />>`, `Fn(...)` и visibility — нормальная работа текущего lexer/parser, плохо названная HACK. Statement/path macro handling связано с `pin!`-группой. |
| Macro matcher | `macro_rules_macro_rules.cpp:534,833,1537,2311,2313,3811` | Реальные opaque-fragment и matcher-state обходы; строка 534 привязана к ICU из rustc 1.90. |
| Hygiene/macros | `ident.cpp:9`, `synext_macro.cpp:1559,2094` | Неполная hygiene и hardcoded `format_args!` API. |
| Resolver | `resolve_common.cpp:98,543`; `resolve_main_bindings.cpp:411,641,1512,1702,1760,1851,1860,2681,3792,3858,3891,4096` | Синтетические `=crate`, anon-module и primitive-module paths — единый долг модели путей. |
| Decorators | `synext_decorator.cpp:1555,2475,2612,2972` | Повторный обход anon modules и частный workaround для `windows-0.48`. |
| HIR layout | `hir_asm.h:10`, `hir_expr.h:580`, `hir_type.cpp:3`, `hir_visitor.cpp:142` | В основном границы файлов и explicit staging; не самостоятельные bugs. |
| HIR lowering | `hir_from_ast.cpp:176,562,589,590,1620,3041,3631` | Self/HRTB sentinels, синтетический trait bound, null HIR pointer как discriminator. |
| HIR conversion | `hir_conv_main_bindings.cpp:657,949,1875,2372,2381,2724,3244,4144,4356` | Lifetime heuristics, `#` в enum path, approximate DST и privacy bypass `fmt::rt::Argument`. |
| HIR expansion | `hir_expand_main_bindings.cpp:114,4553,6867,8690` | Literal-false experiments удалены; active invalid-unsize, generic-array metadata и placeholder fallback остаются. |
| HIR identity | `hir_hir.cpp:203,697`; `hir_path.cpp:326`; `hir_type.cpp:1475,1481,1490` | Const ordering, нетранзитивный HRTB order и fuzzy type relation — высокий риск. |
| Metadata | `hir_main_bindings.cpp:1075,1572,1588` | `.hir` suffix — соглашение custom metadata, не дефект; empty crate-name rewrite — compatibility debt. |
| Typeck common | `hir_typeck_common.cpp:503,515,753,761` | Erasure и passthrough lifetime вместо явных binders. |
| Typeck solver | `hir_typeck_expr_cs.cpp:932,2030,4243,4935,5801,5815,6586,7438,7898,8042,8222`; header `:175` | Остались active heuristics и DEBUG-текст; operator result=LHS и arbitrary ivar fallback требуют отдельных units. |
| Typeck helpers | `hir_typeck_helpers.cpp:6025,7183` | Opaque fuzzy matching и array→slice shortcut вместо нормального receiver adjustment. |
| Typeck impls | `hir_typeck_main_bindings.cpp:2121,2349,2383` | Lifetime bounds копируются/заменяются для совпадения представления. |
| Static solver | `hir_typeck_static.cpp:478,508,1241,2085` | Associated bounds дописываются, `_` автоматически проходит bound, opaque equality обходится локально. |
| Const eval | `hir_conv_constant_evaluation.cpp:1326,2198,3448,3463` | One-past-end допустим; ignore Drop и «roughly-correct» monomorph state — реальные пробелы. |
| MIR lowering | `mir_from_hir.cpp:203,1109,1594,1621,6225,6258,8888`; header `:41` | Drop flags, unsize metadata, match flattening и hardcoded Box move. |
| MIR validation | `mir_helpers.cpp:626` | Validator скрывает отсутствие корректного validity analysis. |
| MIR passes | `mir_operations.cpp:2804,3630,3866,3943,4011,4037,4305,4748,5510,5557,9849,9936` | Box layout coupling, generic metadata и drop removal опасны; 32-bit usize masking и conservative inlining сами по себе нормальны. |
| Mono MIR backend | `trans_codegen.cpp:492` | Пустой основной output — marker рядом с `.mir`, не самостоятельная потеря кода. |
| C backend | `trans_codegen_c.cpp:348,595,1280,1291,1500,1903,2089,2526,2887,2893,3154,5291,5930,6719,7235` | Hardcoded std ABI, принудительный `-O1`, platform workarounds, fake `.rlib` marker и runtime `abort`. |
| Enumeration | `trans_main_bindings.cpp:1349,2204,2487,2872` | Generated statics, `caller_location`, default trait bodies и lifetime population обходят неполную dependency model. |
| Mangling | `trans_mangling.cpp:70,72,254` | Потенциальные symbol collisions. |

Следующая последовательность: убрать исполняемый `tracing_attributes` bypass как часть macro-hygiene/proc-macro модели; затем переименовать ложные `HACK` в документированные инварианты; реальные HACK исправлять только unit-first, по измеряемому общему эффекту.
