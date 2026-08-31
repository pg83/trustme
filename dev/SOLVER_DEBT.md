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

## Итог

2026-09-01: инвентарь пуст. Все решающие tri-state пути удалены (пп. 1,
2, 5 — кодом; пп. 3, 4, 6, 7 — аудитом признаны консервативными
префильтрами/трансляциями). Финальный независимый deep-dive: legacy-имена
0 вхождений, `certaintyFromCompare` 0, все потребители `Ambiguous`
(coercion, associated, inherent, autoderef, getValue, мосты static/trans)
консервативны — defer/stall/retry, выборов по неоднозначности нет.
unit зелёный, static gate 0/0, libcore быстрее базлайна.

## Закрытые пункты

(перечислены выше со статусом ЗАКРЫТ)

## Проверено и признано легитимным

- `StaticTraitResolve::getValue`: `Ambiguous`/bounded impl → `NotYetKnown`
  (`hir_typeck_static.cpp:1899`), inherent `Ambiguous` → `NotYetKnown`
  (`:1965`) — решение по неоднозначному ответу не принимается.
- `checkCoerceTys`/`checkUnsizeTys` — тонкие адаптеры над
  `SolverCoercionAdjustment` (коммит `6dc5c40c4`); собственного обхода типов
  нет, AST rewrite исполняет план солвера.
