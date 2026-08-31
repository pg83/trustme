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

## Закрытые пункты

(перечислены выше со статусом ЗАКРЫТ)

## Проверено и признано легитимным

- `StaticTraitResolve::getValue`: `Ambiguous`/bounded impl → `NotYetKnown`
  (`hir_typeck_static.cpp:1899`), inherent `Ambiguous` → `NotYetKnown`
  (`:1965`) — решение по неоднозначному ответу не принимается.
- `checkCoerceTys`/`checkUnsizeTys` — тонкие адаптеры над
  `SolverCoercionAdjustment` (коммит `6dc5c40c4`); собственного обхода типов
  нет, AST rewrite исполняет план солвера.
