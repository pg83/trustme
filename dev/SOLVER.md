# SOLVER.md — единый goal solver и инференс без эвристик

Typeck пользуется одним goal solver. Через границу солвера выходят только
`Proven`, `Ambiguous`, `NoSolution` и типизированные данные ответа.
`Ambiguous` — состояние управления, не ответ: потребитель обязан
defer/stall/retry (данные остаются в obligations и пересматриваются при
новых фактах); принимать решение — выбирать кандидата, применять impl,
считать доказанным — можно только по `Proven`/`NoSolution`.

## Граница и ответ

- Все type/const inference variables входной цели канонизируются.
- Ответ трёхчастный, misuse непредставим в типах: `SolverResponse` —
  только effects (certainty, type/const slots, obligations, type/value
  equalities, операторный агрегат), поля impl у него нет;
  `SolverSelection{effects, const SolverImpl& impl}` — доказанный и
  выбранный impl, существует только при `Proven`; `SolverMayApply` —
  явный probe «мог бы примениться», кандидат опционален и выбором не
  является. Индивидуальных ambiguous candidates в ответе нет; при
  нескольких кандидатах экспортируется только пересечение их общих
  slots/equalities/obligations (для literal-ivar — ∀-квантор по всем
  viable-кандидатам).
- `Context::applySolverResponse(const SolverResponse&)` — единственная
  точка применения inference-эффектов к caller table.
- Coercion/unsize передаются в `TraitGoalQuery` как данные relation и
  решаются внутри солвера; expression typeck только материализует
  возвращённый `SolverCoercionAdjustment` в HIR-узлы.
- Кэш хранит полный неизменяемый canonical response.
- `StaticTraitResolve` — мост в этот же солвер, не второй решатель;
  `canUnsize` ставит обычную цель `Unsize<dst>` и принимает только
  `Proven`.

## Candidate assembly и relation

- Голова каждого кандидата (trait impl, ParamEnv, builtin, alias-bound,
  trait-object, opaque, magic) проходит транзакционную унификацию
  (`relateAssembledHead`/`unifyImplHead`) под snapshot; экспортируемые
  head equalities доказываются evaluator-ом.
- Префильтры (HIR-impl-индекс, `probeTypeRelation`/`probeParamRelation`)
  отклоняют только доказанный `NoSolution` и не влияют на certainty.
  Structural tri-state сравнений (`compareWithPlaceholders`-класс) в
  решающих путях нет; `HIRCompare` живёт только в структурном компараторе
  (`hir_type.cpp`/`hir_path.cpp`) и реализациях `HIRMatchGenerics` с
  точными исходами.
- Override assoc-биндинга при неточном совпадении params несёт попарные
  typed equalities, которые evaluator обязан доказать; effectful-кандидаты
  не дедуплицируются с другими путями доказательства.
- Builtin `Sized`/`Copy`/`Clone`/`Unsize`/`CoerceUnsized` — правила
  evaluator-а: структурные формы порождают вложенные obligations
  (per-element, DST-хвост, pointee relation), generic-параметры идут
  обычным ParamEnv-путём. Certainty-only запросы `typeIsSized/Copy/Clone`
  возвращают `SolverCertainty` и мемоизируются по указателю
  интернированного типа с инвалидацией по поколениям
  eatCache/ivars/solverEnv; кэшируются только effect-free не-Ambiguous
  ответы.

## Инференс выражений

Из дерева выражений строится ruleset (equate/coerce/assoc), дальше
фикспойнт. Отложенность — данные, объявленные в типах: `Coercion`-узлы в
`linkCoerce`, `linkAssoc` + `Associated::stalledOn`, solver deferred
coercions, `ValuePtr::NotYetKnown`. Исключений-«результатов» нет.

Финализация (`finaliseIvarCoercions`) применяет правила над view из живых
obligations (`IvarCoercionIndex`, перевычисляется из `Coercion`-узлов
через `evaluateCoercionGoal`); параллельного хранилища кандидатов нет:

- совместная унификация: если все оставшиеся кандидаты попарно
  унифицируемы транзакционным probe (named fn items — только при точном
  равенстве), все они реально унифицируются между собой и с ivar;
- identity-commit: несвязанная коэрция берёт тип источника — только в
  финальной фазе, либо раньше при identity-свидетеле (endpoint по обе
  стороны коэрции);
- pointer-LUB — явная решётка форм `&mut T ≤ &T ≤ *const T`,
  `&mut T ≤ *mut T ≤ *const T`; несравнимые формы = ambiguity; pointee —
  единственный минимальный общий член Deref/unsize-цепочек,
  неединственность = ambiguity;
- два и более fn-item источника — fn-pointer LUB;
- `!` — дно решётки коэрций: не typing hint и не identity/equality
  endpoint.

Ранжирования, «взять первого», «наименее рестриктивного» не существует.

## Фолбэки

Только явные языковые правила, в самом конце, когда obligations больше
ничего не дадут:

- незакреплённые literal-ivar: int → `i32`, float → `f64`
  (`applyDefault`, откладывается `numericDefaultMustWait`);
- pre-2024 never-fallback `!` → `()` — настоящая type-only коэрция;
- объявленные generic defaults — применяются только при единогласии всех
  дефолтов класса эквивалентности; разногласие остаётся ambiguity;
- RPIT defaulting.

Остаточная неоднозначность после фолбэков — ошибка компиляции с
диагностикой, не выбор.

## Инварианты

- Tri-state/fuzzy результат не превращается в решение ни под каким именем;
  эвристика, перенесённая или переименованная, — нарушение.
- Префильтр отклоняет только доказанное и не влияет на certainty.
- `Ambiguous` + `impl` в ответе не применяется как выбранный: потребители
  делают defer/stall/retry (`NotYetKnown`, retry autoderef/inherent,
  `CoerceResult::Unknown`).
- Классификация (operator summary и т.п.) требует `Proven`; fuzzy
  деградирует только в консервативную сторону.
- Новые «default»-механики допустимы только как объявленные языковые
  правила с единогласием/единственностью и применением последними.

## Gates

- C++ UT компиляцией фиксирует точную форму `SolverResponse` и
  `TraitGoalQuery`, а также единственный overload
  `Context::applySolverResponse`.
- Семантические Rust-регрессии: ambiguity, ParamEnv, projections,
  coercion/unsize, operators, inherent methods, static consumers,
  literal/never fallback.
- Static gate: 0 статических объектов / 0 writable bytes.
- Полный Nix `unit` зелёный.

## Method lookup

Перечисление кандидатов метода — candidate assembly солвера
(`evaluateMethod`): inherent impls, ParamEnv bounds, trait objects с
supertraits, erased/opaque declared traits, bound projections, in-scope
traits; применимость каждого — транзакционные головы/obligations.
Лестница autoderef/borrow-эскалации — документированный порядок проб из
семантики языка; каждый шаг ставит method-goal новому receiver. Дедуп —
только по identity доказанного маршрута; несколько разных применимых
кандидатов на одном шаге — «multiple applicable items in scope»;
`supertrait_item_shadowing` — правило фичи (строгий подтрейт затеняет
декларации супертрейтов). Current-trait preference не существует —
вызовы своих методов покрывает ParamEnv-кандидат `Self: Trait`.
Custom receiver проверяется транзакционной унификацией объявленной
receiver-формы с подстановкой Self (допустимые формы задокументированы
рядом с правилом: `Self`, `&Self`, `&mut Self`, `Box/Rc/Arc<Self>`,
`Pin<P>`, raw pointers, `Receiver`/`Deref`-цепочка); generic-биндинги
receiver — из той же унификации. Inherent-cache — грубый индекс формы,
семантических решений не принимает.

## Перф-методика

Замер компиляции libcore: rustc на `library/core/src/lib.rs` из
`.build/tst/rust-src.tar` с `-O --crate-name core --crate-type rlib
--crate-tag 0_0_0 -C emit-cpp-only -C emit-link-manifest=… --edition
2024`, внутри `nix develop .#clang`, `/usr/bin/time -f 'wall=%e
maxrss=%M'`. Машина бимодальна — сравнивать только интерливленными
парами против эталонного бинаря. Уровень: 40.8–41.3 с быстрого режима,
допуск +5%.
