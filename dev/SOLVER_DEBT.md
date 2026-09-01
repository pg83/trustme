# SOLVER_DEBT — инвентарь legacy/fuzzy путей солвера

Единственный источник правды о прогрессе выкорчёвывания tri-state
(`HIRCompare::Equal/Fuzzy/Unequal`) решений из солвера/тайпчека.
Пункт вычёркивается только гейтом (Claude) после личной верификации:
семантика удалена (не переименована/не переехала в адаптер), unit зелёный,
static gate 0/0, libcore ≤ базлайна.

Определение долга: место, где tri-state структурный результат превращается в
решение солвера — `SolverCertainty`, выбор/применение кандидата или impl,
доказательство obligation. Низкоуровневое структурное сравнение в
`hir_type.cpp`/`hir_path.cpp`, не влияющее на выбор, — легитимно.

Строки кода — на момент аудита 2026-08-31 (HEAD `6dc5c40c4`); перегрепать.

## Перф-базлайн

Замер: rustc на HEAD, `library/core/src/lib.rs` из `.build/tst/rust-src.tar`,
`-O --crate-name core --crate-type rlib --crate-tag 0_0_0 -C emit-cpp-only
-C emit-link-manifest=… --edition 2024`, внутри `nix develop .#clang`,
`/usr/bin/time -f 'wall=%e maxrss=%M'`.

- базлайн (HEAD `6dc5c40c4`, 2026-08-31): wall 41.2 с (41.20/41.23/41.66),
  maxrss 1 844 900 KiB. Порог: wall ≤ 43.3 с (+5%).

## Открытые пункты

### 1. Builtin Sized/Copy/Clone доказуются рекурсивным tri-state кодом
- ЗАКРЫТ 2026-08-31, верифицирован гейтом. `assembleMagicCandidatesCb`
  теперь только предлагает shape-gated кандидата; доказательство — в
  evaluator (`evaluateBuiltinSizedCopyClone`): структурные правила порождают
  obligations (tuple per-element, array inner, DST-хвост → `Sized`), generic
  Sized — ParamEnv-кандидат по объявленному `isSized`, generic Copy/Clone —
  обычный ParamEnv-путь. `typeIsSizedBuiltin`/`typeIsCopyBuiltin` и
  tri-state тело `typeIsClone` удалены; обёртки `typeIsSized/Copy/Clone` —
  чистые solver-запросы (`solveTraitGoalCertainty`); структурный путь
  (`solveStructuralTraitGoalCertainty`) — только bootstrap без lang item.
  Игнор `_cmp` в generic-ветках удалён вместе с ветками.
- Верификация: unit exit 0 (полный), static gate 0/0, libcore
  42.1/42.3/43.1 с (базлайн 41.2, порог 43.3), maxrss 1 851 324 KiB.
- Хвост на заметку: generic группы ≥2 раньше получали builtin
  `Sized=Equal`, теперь NoSolution (ParamEnv покрывает группы 0/1);
  следить за GENERICPlaceholder в следующих пунктах.

### 2. CoerceUnsized pointer-кандидат через compareWithPlaceholders
- ЗАКРЫТ 2026-08-31, верифицирован гейтом. Кандидат предлагается только по
  форме (оба Pointer, ослабление мутабельности); равенство inner-типов
  устанавливает evaluator через `evaluateBuiltinCoerceUnsized` →
  `relateTypes` (typed effect/equality в ответе). `certaintyFromCompare`
  по bin/rustc: 0 вхождений.
- Верификация: unit exit 0 (полный), static gate 0/0, libcore интерливленно
  против HEAD-базлайна: 41.2–41.6 у базлайна, медиана 42.1 у пропатченного
  (+2.2%, порог 43.3).

### 3. ParamEnv assoc-projection кандидаты через fuzzy-гейт
- ЗАКРЫТ 2026-08-31 аудитом (кода не требовалось). Единственный решающий
  потребитель `iterateBoundsTraits` (`:4978` после п.1) отклоняет только по
  `Unequal` и игнорирует cmp дальше; собранный кандидат идёт через
  `collect(ParamEnv)` → `relateAssembledHead` (транзакционный Unifier).
  Внутренний гейт `iterateBoundsTraitsCb` — тоже reject-only-on-Unequal.
  Инвариант: ни один потребитель не имеет права использовать cmp иначе,
  чем для отклонения доказанного Unequal.

### 4. Callable/Fn-семейство: сравнение входов через compareWithPlaceholders
- ЗАКРЫТ 2026-08-31 аудитом (кода не требовалось): `relateAssembledHead`
  (`:9448`, вызов из `collect(...)` в `assembleCandidates`) гоняет
  транзакционный `Unifier` по goalType/goalParams против головы КАЖДОГО
  кандидата assembly (Builtin/Other/ParamEnv/AliasBound), экспортируя
  headEqualities, которые `evaluateCandidate` проверяет. Сравнения на
  `:3292`/`:3366` (и `:9851`) — консервативные префильтры: отклоняют только
  доказанный Unequal, ничего не доказывают. Инвариант, который обязан
  сохраняться: префильтр не имеет права влиять на certainty.

### 5. comparePp-гейты на sourceTrait params
- ЗАКРЫТ 2026-09-01, верифицирован гейтом. Выбран typed-effects дизайн:
  при `Equal` override применяется напрямую, при `Fuzzy` вместе с override
  на кандидата навешиваются попарные type/const equality-обязательства
  (`appendAssembledParamEqualities` → headEqualities), которые
  `evaluateCandidate` доказывает транзакционно — провал убивает кандидата
  вместо молчаливо неверных typeBounds. Effectful-кандидаты помечены
  `assemblyEffectful` и исключены из дедупа `pushCandidate`, чтобы
  альтернативные пути не склеивались в неявный AND. Остаточная
  консервативность: при недоказуемом equality кандидат умирает, а
  вариант без override не собирается — потеря полноты, не корректности.
- Верификация: unit exit 0 (полный), static gate 0/0, libcore
  интерливленно против контроля на той же машине: +1.9% кумулятивно
  (тот же дрейф, что после п.2 — сам п.5 нейтрален).

### 6. Builtin operator: выход через compareWithPlaceholders == Equal
- ЗАКРЫТ 2026-08-31 аудитом. `operatorImplHasBuiltinSignature`
  (`hir_typeck_helpers.cpp:12759`) — строгий `== Equal`; Fuzzy всегда
  деградирует в «семантический impl», а единственный потребитель
  (`hir_typeck_expr_cs.cpp:1372`, `canContextualisePrimitiveRhs` и
  magic-inference шорткаты) при этом ОТКЛЮЧАЕТ шорткат — консервативно:
  возможна потеря полноты инференса, но не неверное решение. Инвариант:
  строгость `== Equal` сохранять; трактовка Fuzzy как builtin стала бы
  решением по fuzzy.

### 7. Tri-state обёртки typeIsSized/typeIsCopy/typeIsClone в решениях тайпчека
- ЗАКРЫТ 2026-08-31 аудитом: долг устранён пунктом 1. Обёртки стали 1:1
  трансляцией `SolverCertainty` → `HIRCompare` (`solveTraitGoalCertainty`),
  второго решателя нет. Все решающие потребители проверены и честны с
  tri-state: гейты только по доказанному `Equal`/`Unequal`
  (`hir_typeck_expr_cs.cpp:1938`, `:2059`, `:5272`, `:5516`), Fuzzy у
  implicit-Sized кандидата превращается в obligation + ambiguity
  (`hir_typeck_helpers.cpp:11517`). Возвращаемый тип `HIRCompare` —
  косметика интерфейса, не семантика; замена на `SolverCertainty` —
  необязательный рефакторинг вне долга.

## Перф-возврат после миграции

2026-09-01: профиль (`perf record`) подтвердил регрессию в полном
Sized-пути (`typeIsSized` 2.43% → 4.27% inclusive). Возврат: certainty-only
запросы `typeIsSized/Copy/Clone` идут через
`evaluateStructuralTraitCertainty` — безусловные листовые fast-path для
форм, где полный evaluator даёт Proven без effects, плюс мемоизация
effect-free не-Ambiguous ответов в evaluator-owned `IntMap`
(ключ — указатель интернированного типа; инвалидация по поколениям
eatCache/ivars/solverEnv; никаких static). Гейт-замер интерливленно:
базлайн 41.26–42.19 с (медиана 41.50), после 40.86–41.25 с (медиана
40.95), maxrss 1 839 572 KiB против 1 844 900. Итог: быстрее и легче
дорефакторингового базлайна.

## Итог (ОТОЗВАН 2026-09-01)

Закрытия пп. 6 и 7 «аудитом» отозваны гейтом: они нарушали собственный
критерий («эквивалент под любым именем — не удаление»). Переоткрыты как
пп. 8–9, плюс п.10 — префильтры. Пп. 1, 2, 5 (код) и 3, 4 в части
транзакционной унификации остаются в силе.

## Итог (второй заход, 2026-09-01)

Инвентарь пуст, включая переоткрытые пп. 8–9. Финальная карта tri-state
по bin/rustc:
- `compareWithPlaceholders`/`comparePp`/`certaintyFromCompare` в решающих
  путях солвера/тайпчека: 0. Решают транзакционные probes
  (`probeTypeRelation`/`probeParamRelation`) и унификация голов.
- `HIRCompare` остался только в: (а) hir_type/hir_path/hir_type_ref —
  структурный компаратор (вырезан определением долга); (б) реализациях
  интерфейса `HIRMatchGenerics` (RpitOriginMonomorph — точный,
  GetSelf/MCB/inherent-cache/ProvenGenericParamMatcher — точные,
  `ImplMatcher` в hir_hir.cpp — индексный префильтр crate-impl и матчер
  голов specialization: отклоняет только доказанный Unequal, каждый
  выживший impl проходит транзакционный `unifyImplHead`).
- Solver-запросы (`typeIs*`) — `SolverCertainty`; структурное свойство
  `typeIsInteriorMutable` — семантический `InteriorMutability`.
Инварианты, которые обязаны сохраняться: префильтры отклоняют только
доказанный Unequal и не влияют на certainty; matcher-инфраструктура не
рождает решений из Fuzzy.
Перф: интерливленные минимумы 41.03 (дорефакторинговый базлайн) против
40.79 (итог), maxrss −6 МБ. unit зелёный, static gate 0/0.

## Открытые пункты (второй заход)

### 8. Structural tri-state сравнения в решающих файлах солвера
- ЗАКРЫТ 2026-09-01, верифицирован гейтом. `comparePp` удалён (заменён
  `probeParamRelation`/`probeTypeRelation` — транзакционный Unifier со
  snapshot/rollback); callable/closure префильтры — probe или удалены
  (решает унификация головы); override assoc-биндингов — probe + typed
  effects; operator-классификация — `probeTypeRelation == Proven`;
  `iterateBoundsTraitsCb` — probe, `HIRCompare` удалён из
  `TraitBoundCallback`; `findNamedTraitInTraitCb` (static) — точное
  равенство интернированных params + match-generics только с `Equal`;
  coercion hint в expr_cs — probe; RPIT reveal — только `Equal`, второй
  матч = ASSERT_BUG; мёртвый `compareTy` удалён.
  Grep `compareWithPlaceholders|comparePp` по helpers/static/expr_cs: 0.
- Верификация: unit exit 0, static gate 0/0, libcore интерливленно —
  минимумы 41.24 (базлайн) против 41.13 (после), maxrss ниже на ~6 МБ.

(исходная формулировка ниже)
- Остаточные `compareWithPlaceholders`/`comparePp` в
  `hir_typeck_helpers.cpp`: `:3317`, `:3391` (callable-префильтры),
  `:3607`, `:3647`, `:9938` (гейт override, Equal-ветка выбирает без
  effects), `:9899` (alias-bound префильтр), `:12856`
  (operator-классификация — fuzzy МЕНЯЕТ поведение инференса, это
  решение), внутренность `iterateBoundsTraitsCb` (`:2947` и далее).
- Требование: ноль structural tri-state сравнений в решающих путях.
  Префильтры — либо убрать (головы и так унифицируются транзакционно),
  либо заменить на transactional unification probe с идентичным исходом;
  operator-классификация — на solver/унификатор.
- Статус: ОТКРЫТ.

### 9. Tri-state HIRCompare в интерфейсах тайпчека
- ЗАКРЫТ 2026-09-01, верифицирован гейтом. `typeIsSized/Copy/Clone`
  возвращают `SolverCertainty`; все потребители переведены 1:1
  (Equal→Proven, Fuzzy→Ambiguous, Unequal→NoSolution), мост
  `NextSolverBridge::typeIsCopy` — `== Proven`. `typeIsInteriorMutable`
  (структурное свойство UnsafeCell, не солвер) — собственный enum
  `InteriorMutability {No, Yes, Unknown}` по всем потребителям. Мёртвые
  `compareTy`/`compareValue` удалены. `HIRCompare` в typeck-файлах —
  только реализация интерфейса `HIRMatchGenerics` (matchTy/matchVal,
  исключительно точные Equal/Unequal, Fuzzy не рождается и не
  потребляется) — инфраструктура структурного матчера дженериков.
- Верификация: unit exit 0, static gate 0/0, libcore интерливленно —
  минимумы 41.03 (базлайн) против 40.79 (после), maxrss −6 МБ.

(исходная формулировка ниже)
- `typeIsSized/typeIsCopy/typeIsClone` возвращают `HIRCompare` —
  эквивалент SolverCertainty под legacy-именем; потребители сравнивают с
  Equal/Fuzzy/Unequal. Заменить тип на `SolverCertainty` по всем
  потребителям typeck (expr_cs, helpers, static-мост); post-monomorph
  bool-мосты остаются bool. Цель: `HIRCompare` не упоминается в
  hir_typeck_helpers.h интерфейсах решателя и его потребителях.
- Статус: ОТКРЫТ.

## Открытые пункты (третий заход, 2026-09-01): движок угадывания типов

Перф-контроль этого захода: бинарь HEAD `fe2a7712f`, уровень 40.8–41.3 с
на libcore, порог +5%. Замер интерливленными парами, методика выше.

### 10. Ядро: possibleIvarVals / checkIvarPoss
- `hir_typeck_expr_cs.cpp:1966` (`checkIvarPoss`), `hir_typeck_expr_cs.h`
  (`Context::IVarPossible`, `possibleIvarVals`): выбор типа для
  неразрешённого ivar из множеств coerce-from/coerce-to — ранжирование
  `TypeRestrictiveOrdering` (`:434`; её `tagOrdering`-таблица числится в
  allowlist static gate — удалить оттуда в финале), флаги `selectable`,
  барьеры `forceNoFrom/forceNoTo/forceDisable`, пять эскалирующих
  fallback-раундов `:6115–:6201`: None → Backwards → Assume →
  IgnoreWeakDisable → FinalOption.
- Замена: явные литеральные фолбэки (int/float/`!`) как документированные
  правила + доведение вывода через solver/coercion obligations; настоящая
  неоднозначность — ошибка с диагностикой.
- Статус: ОТКРЫТ.

### 11. Фидеры движка
- `possibleEquateTypeUnknown` / `IvarUnknownType::From/To/Bound` — 12+
  мест (`:981–:988`, `:1440–:1476`, `:4488` и далее по grep).
- Умирают вместе с п.10; до того — по мере снятия раундов.
- Статус: ОТКРЫТ.

### 12. Magic inference links и coercionHints-equate
- «Magic inferrence link» эвристики операторов/примитивов (`:1396`,
  `:1420`, `:1433`) — приравнивание типов мимо солвера.
- Жадный equate по `coercionHints` (probe-гейт есть, но сама механика —
  эвристика, не obligation).
- Статус: ОТКРЫТ.

Прогресс п.10 (2026-09-01, шаг «а», верифицирован гейтом): 10 из 11
мёртвых ветвей удалены (−191 строка), вместе с осиротевшими
`PossibleType::isDestS`/`nDstIvars`. `never_fallback_unit` оказался живым
(`never_type_fallback_to_unit`): это НЕ угадывание, а языковое правило
pre-2024 never-type fallback `!`→`()` — при сносе каркаса оформляется
явным правилом, не умирает. Unit 1035/1035, static gate 0/0, перф —
паритет в обоих режимах машины.

Прогресс п.10 (2026-09-01, шаг «б», верифицирован гейтом): эскалационная
лестница удалена целиком — раунды Backwards/Assume/IgnoreWeakDisable/
FinalOption, `IvarPossFallbackType`, ветви fallback_single_source,
rank_source_most_permissive, ignore_disable_arbitrary,
ignore_weak_single_option, ignore_disable_only_source, fallback-активации
most_accepting_pointer, raw_pointer_fallback (−115 строк). Вместо них —
правило совместной унификации: если все оставшиеся selectable-кандидаты
попарно унифицируемы транзакционным probe (named fn items — только при
точном равенстве), все они реально унифицируются `equateTypes` между
собой и с ivar; для ≥2 fn-item источников — честный fn-pointer LUB.
GUESS-остатков 0. `TypeRestrictiveOrdering` пока жива (3 потребителя в
раунде None) — умирает на шаге «г»/«е» вместе с tagOrdering в allowlist.
Unit 1033/1033, static gate 0/0, перф — паритет (мин. 40.93 vs 41.03).

Прогресс п.12 (2026-09-01, шаг «в», верифицирован гейтом): все три
magic-линка операторов удалены (binop 13k, uniop 591, borrowed 1) вместе
с `H::typeIsNum`, shift-shortcut и `canContextualisePrimitiveRhs`.
Замена — правила: из Ambiguous typed response экспортируются только
отношения, общие для ВСЕХ viable-кандидатов и касающиеся literal-ivar
(∀-квантор, не выбор); конкретный associated output ограничивает Self
через typed response; вложенный coercion-ivar даёт подсказку только при
единогласии всех selectable endpoints; литеральный int→i32/float→f64
fallback уже существовал в финальной фазе (`applyDefault`) и не менялся.
Двойной solver-проход оператора устранён (response переиспользуется).
GUESS-остатков 0. Unit 1033/1033, static gate 0/0, перф: мин. 40.79 vs
41.03 у базлайна, RSS +2.6 МБ. Из п.12 остаётся только
coercionHints-equate (шаг «д»).

Прогресс п.10 (2026-09-01, шаг «г», верифицирован гейтом): раунд None
переведён на правила; `TypeRestrictiveOrdering` и `tagOrdering` удалены
физически (grep 0 в исходниках и бинаре), allowlist-строка снята из
static gate. Правила: identity-commit только в финальной фазе (несвязанная
коэрция берёт тип источника — семантика rustc), либо раньше при
identity-свидетеле (endpoint по обе стороны коэрции) + совместной
унификации; pointer-LUB — явная решётка форм `&mut T ≤ &T ≤ *const T`,
`&mut T ≤ *mut T ≤ *const T` (несравнимые формы = ambiguity), pointee —
единственный минимальный общий член Deref/unsize-цепочек (неединственность
= ambiguity); `!` — дно решётки коэрций, не участвует как identity/equality
endpoint; литеральный/never fallback — последними. 8 классов красных
тестов закрыты правилами, GUESS 0. Unit 1033/1033 + 89/89, static gate
0/0 без tagOrdering-записи, перф: все 3 пары быстрее базлайна
(43.9–44.1 vs 44.4–44.8), RSS +2.3 МБ.

Прогресс п.12 (2026-09-01, шаг «д», верифицирован гейтом): канал
`coercionHints` удалён целиком (map, API, все producer/consumer; grep 0).
Ожидаемый тип приходит настоящей коэрцией (`linkCoerce`): struct literal,
match, unary-`!`, method-probing (транзакционный, destination только для
probe, естественный результат метода сохраняется, pointer-связь по
решётке). Новые правила solver-уровня: уникальная конкретная направленная
CoerceTo-граница разрешает компонент до финального identity; outer
structural unification ждёт разрешения вложенного coercion source;
obligations переносятся при слиянии ivar-классов. GUESS 0. Unit
1033/1033 + 89/89, static gate 0/0, перф: new быстрее во всех парах
(40.6–40.9 vs 41.2–44.4). П.12 закрыт целиком.

### Карта срабатываний (инструментация 2026-09-01, corpus libcore+liballoc+libstd)

Всего 17 239 успешных приравниваний (успех = сдвиг mutationGeneration):
- magic_primitive_binop 13 003 (75.4%); magic_numeric_uniop 591 (3.4%);
  magic_borrowed_primitive 1 (мёртв практически).
- Раунд None 2 767 (16.1%), из них pre_removal_single_source 2 655 (96%);
  остальное: source_destination_exact 44, most_accepting_pointer 34,
  only_remaining_option 32, function_pointer_merge 2.
- coercionHints equate 435 (2.5%, в alloc 12.6%).
- Assume 194 (1.1%) — все через rank_source_most_permissive; в примерах
  кандидаты почти всегда взаимно унифицируемы (NonNull<u8> vs NonNull<_77>).
- IgnoreWeakDisable 131, из них ignore_disable_arbitrary 66 (!),
  ignore_weak_single_option 42.
- FinalOption 59 (fallback_single_source 55, most_accepting_pointer 3,
  raw_pointer_fallback 1); Backwards 58 (все fallback_single_source).
- Мёртвые на корпусе ветви (11): single_each_concrete_source/destination,
  single_each_ivar_source/destination, rank_destination_most_restrictive,
  assume_rank_all, duplicate_source_destination, never_fallback_unit,
  never_fallback_diverge, coerce_source_deref_unsize_destination,
  ignore_disable_only_target.

План резки (сверху — раньше): (а) мёртвые ветви; (б) хвост лестницы
Backwards/Assume/IgnoreWeakDisable/FinalOption — правило «если все
оставшиеся кандидаты взаимно унифицируемы, унифицировать их все» покрывает
большинство примеров, произвол (ignore_disable_arbitrary) умирает;
(в) magic binop/uniop → solver operator obligations + литеральный fallback;
(г) None/single-source → принципиальное разрешение coercion-obligation;
(д) coercionHints; (е) снос каркаса + tagOrdering из allowlist.

## Закрытые пункты

(перечислены выше со статусом ЗАКРЫТ)

## Проверено и признано легитимным

- `StaticTraitResolve::getValue`: `Ambiguous`/bounded impl → `NotYetKnown`
  (`hir_typeck_static.cpp:1899`), inherent `Ambiguous` → `NotYetKnown`
  (`:1965`) — решение по неоднозначному ответу не принимается.
- `checkCoerceTys`/`checkUnsizeTys` — тонкие адаптеры над
  `SolverCoercionAdjustment` (коммит `6dc5c40c4`); собственного обхода типов
  нет, AST rewrite исполняет план солвера.
