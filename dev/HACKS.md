# Открытые HACK-маркеры компилятора

Аудит ограничен `bin/rustc/**`. Здесь остаются только нерешённые места; история удалённых compatibility-веток и закрытых исправлений находится в git.

## Самые опасные места

1. Macro hygiene фактически неполна.

   [ident.cpp](/home/pg/monorepo/trustme/bin/rustc/ident.cpp:9) реализует только примитивную видимость контекстов. [synext_macro.cpp](/home/pg/monorepo/trustme/bin/rustc/synext_macro.cpp:2092) вырезает hygiene из строкового представления.

2. HRTB/lifetime relation местами математически некорректна.

   В [hir_path.cpp](/home/pg/monorepo/trustme/bin/rustc/hir_path.cpp:326) `TraitPath::ord` считает два пути равными, если хотя бы у одного есть `#apply_elision`. Такое равенство нетранзитивно и нарушает требования `std::map/std::set`.

   Тот же кластер включает sentinel `#apply_elision`, синтетический `Self: Trait`, выбор единственного lifetime trait object, passthrough отсутствующих lifetime-параметров и замену lifetime в impl-методах.

3. Trait solver использует глобальное «не знаю — значит fuzzy».

   [hir_type.cpp](/home/pg/monorepo/trustme/bin/rustc/hir_type.cpp:1475) превращает несовпадение тегов в `Fuzzy` для opaque/unbound/placeholder; [hir_hir.cpp](/home/pg/monorepo/trustme/bin/rustc/hir_hir.cpp:737) считает unbounded infer подходящим любому impl; [hir_typeck_static.cpp](/home/pg/monorepo/trustme/bin/rustc/hir_typeck_static.cpp:1251) автоматически принимает bound для `_`.

   Это может оставлять неверные impl-кандидаты. Нужен отдельный goal с результатом `Yes/Ambiguous/No`, а не ослабление отношения типов.

4. Backend местами генерирует программу, которая просто вызывает `abort()`.

   [trans_codegen_c.cpp](/home/pg/monorepo/trustme/bin/rustc/trans_codegen_c.cpp:4634) обрывает `vmov/vexpand/vpexpand` в GCC-like backend.

   Workaround GCC bug в [trans_codegen_c.cpp](/home/pg/monorepo/trustme/bin/rustc/trans_codegen_c.cpp:976) выбирается по компилятору самого trustme, а не по C++-компилятору, запускаемому backend.

5. Mangling допускает потенциальные коллизии.

   [trans_mangling.cpp](/home/pg/monorepo/trustme/bin/rustc/trans_mangling.cpp:254) кодирует значения associated types trait object, но не их имена. `-` и `#` также сознательно сводятся к одному представлению.

`rustc_legacy_const_generics` и `builtin # offset_of` нельзя удалять только из-за старого комментария: соответствующая семантика может присутствовать в исходниках Rust 1.90.

## Реестр по подсистемам

| Подсистема | Файлы и строки | Нерешённое |
|---|---|---|
| Driver | `main_bindings.cpp:1868,2241` | Глобальная настройка crate loader и отключённый повтор lifetime validation. |
| Expansion | `expand_common.cpp:25,137,387,460,949,2117,2248,2456` | Глобальный module context, повторные проходы, ранний `macro_rules` и преобразование inner items в outer. |
| Parser | `parse_common.cpp:263,306,354,388,403,1279,1353` | Statement/path macro handling, `TOK_HASH` между statements и `builtin #` lowering. |
| Macro matcher | `macro_rules_macro_rules.cpp:534,833,1537,2311,2313,3811` | Opaque fragments, `$crate` special name и matcher-state обходы. |
| Hygiene/macros | `ident.cpp:9`; `synext_macro.cpp:1557,2092` | Неполная hygiene и hardcoded `format_args!` API. |
| Resolver | `resolve_common.cpp:98,543`; `resolve_main_bindings.cpp:411,641,1512,1702,1760,1851,1860,2681,3792,3858,3891,4096` | Синтетические `=crate`, anon-module и primitive-module paths. |
| Decorators | `synext_decorator.cpp:1558,2478,2615,2975` | Повторный обход anon modules и частные path/zero-sized-array workarounds. |
| HIR layout | `hir_expr.h:583` | У `ExprNode_Emplace::Noop` не найден producer, но consumers ещё существуют. |
| HIR lowering | `hir_from_ast.cpp:176,562,589,1620,3094,3689` | Self/HRTB sentinels, синтетический trait bound и null HIR pointer как discriminator. |
| HIR conversion | `hir_conv_main_bindings.cpp:657,949,1875,2372,2381,2724,3244,4144,4382` | Lifetime heuristics, `#` в enum path, approximate DST, privacy bypass и hardcoded trait lookup. |
| HIR expansion | `hir_expand_main_bindings.cpp:114,4555,6869` | Closure Copy prepass, placeholder fallback и generic-array size shortcut. |
| HIR identity | `hir_hir.cpp:203,737`; `hir_path.cpp:326`; `hir_type.cpp:1475,1481,1490` | Const ordering, нетранзитивный HRTB order и fuzzy type relation. |
| Metadata | `hir_main_bindings.cpp:1076` | Empty crate-name rewrite остаётся compatibility debt. |
| Typeck common | `hir_typeck_common.cpp:503,515,753,761` | Erasure и passthrough lifetime вместо явных binders. |
| Typeck solver | `hir_typeck_expr_cs.cpp:932,4991,5924,5938,6713,7565,8025,8169,8349`; header `:175` | Global fuzzy relation, operator result=LHS и arbitrary ivar/associated-type fallbacks. |
| Typeck helpers | `hir_typeck_helpers.cpp:7184` | Opaque fuzzy matching. |
| Typeck impls | `hir_typeck_main_bindings.cpp:2118,2346,2380` | Lifetime bounds копируются или заменяются для совпадения представления. |
| Static solver | `hir_typeck_static.cpp:488,518,1251,2122` | Associated bounds дописываются, `_` автоматически проходит bound, opaque equality обходится локально. |
| Const eval | `hir_conv_constant_evaluation.cpp:1523,3639,3654` | One-past-end допуск и lazy/«roughly-correct» monomorph state. `impl const Drop` неотличим от обычного `Drop`: parser не сохраняет constness trait impl (`parse_common.cpp:3450`). |
| MIR lowering | `mir_from_hir.cpp:203,1129,1614` | Generator drop-flag remap, assignment borrow-extension suppression и virtual unsize cast. |
| MIR passes | `mir_operations.cpp:4749` | Usage downgrade при move. |
| C backend | `trans_codegen_c.cpp:1309,1320,1944,2130,2587,2945,2951,5092,5731` | Принудительный `-O1`, platform workarounds, incomplete asm/AVX translation и runtime `abort()`. |
| Enumeration | `trans_main_bindings.cpp:1595,2449,2738,3117` | Generated statics, `caller_location`, default trait bodies и lifetime population обходят неполную dependency model. |
| Mangling | `trans_mangling.cpp:70,72,254` | Потенциальные symbol collisions. |

Следующий unit-first срез macro hygiene — устранить потерю контекста при строковом преобразовании в `synext_macro.cpp:2092`.
