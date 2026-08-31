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
- `hir_typeck_helpers.cpp:4948`: `iterateBoundsTraits(...)` — `cmp != Unequal`
  пропускает bound в кандидаты; проверить, проходит ли кандидат дальше
  транзакционную унификацию или fuzzy тут финален.
- Внутренность `iterateBoundsTraitsCb` (`:2914`): `compareWithPlaceholders`
  — сам итератор tri-state; после п.1/п.3 оценить, останутся ли потребители,
  для которых fuzzy влияет на выбор.
- Статус: ОТКРЫТ (сначала аудит, потом замена).

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
- Аудит 2026-08-31: fuzzy-`comparePp` решает, какой typeBounds-entry
  перекроет assoc-binding кандидата — выбор ДАННЫХ кандидата до унификации.
  Актуальные места (после закрытия п.1 строки сместились):
  `hir_typeck_helpers.cpp:3578`, `:3609` (trait-object/erased supertrait
  в assembleTypeCandidatesCb), `:9877` (assembleAliasBoundCandidates).
  Голова кандидата потом унифицируется (`relateAssembledHead`), но
  неверно выбранный override assoc-биндинга — это неверные typeBounds,
  а не отклоняемая голова. Замена: не выбирать по fuzzy, а либо нести оба
  варианта кандидатами, либо выражать override equality-обязательством.
- Статус: ОТКРЫТ.

### 6. Builtin operator: выход через compareWithPlaceholders == Equal
- Актуальное место `hir_typeck_helpers.cpp:12759`
  (`operatorImplHasBuiltinSignature`): строгий `== Equal`, fuzzy трактуется
  как «семантический impl» для `SolverOperatorSummary`. Это классификация
  для operator fallback, не доказательство; аудит потребителей summary —
  влияет ли неверная классификация при fuzzy на выбор пути.
- Статус: ОТКРЫТ (аудит, низкий приоритет).

### 7. Tri-state обёртки typeIsSized/typeIsCopy/typeIsClone в решениях тайпчека
- Обёртки (`:5022`, `:5151`, `:5243`) зовут solver, но при `NoSolution`
  падают в builtin-walker (п.1) — т.е. builtin остаётся вторым решателем.
- Решающие вызовы: `hir_typeck_expr_cs.cpp:1938`, `:2059`, `:5272`, `:5516`;
  `hir_typeck_helpers.cpp:3138`, `:4633`, `:5564`, `:5887`, `:5895`, `:11521`.
- После п.1: свернуть обёртки в чистые solver-запросы; пост-монолитные
  потребители (trans/mir/expand, `NextSolverBridge::typeIsCopy` в
  `hir_typeck_static.cpp:2147`) остаются bool-мостом над solver-запросом.
- Статус: ОТКРЫТ (блокирован п.1).

## Закрытые пункты

(пока пусто)

## Проверено и признано легитимным

- `StaticTraitResolve::getValue`: `Ambiguous`/bounded impl → `NotYetKnown`
  (`hir_typeck_static.cpp:1899`), inherent `Ambiguous` → `NotYetKnown`
  (`:1965`) — решение по неоднозначному ответу не принимается.
- `checkCoerceTys`/`checkUnsizeTys` — тонкие адаптеры над
  `SolverCoercionAdjustment` (коммит `6dc5c40c4`); собственного обхода типов
  нет, AST rewrite исполняет план солвера.
