# SOLVER.md — единый goal solver

Typeck пользуется одним goal solver. Потребитель не видит множество viable
impl'ов, не выбирает кандидата по `HIRCompare::Fuzzy` и не повторяет legacy
поиск после `Ambiguous`.

Через границу солвера выходят только `NoSolution`, `Ambiguous` или `Proven` и
типизированные данные ответа. `Fuzzy` остался во внутренних структурных
фильтрах; окончательная relation кандидата выполняется `Unifier` под
snapshot и возвращает `Proven`, `Ambiguous` или `Mismatch`.

## Ответ и граница

- Все type/const inference variables входной цели канонизируются.
- `SolverResponse` содержит certainty, type/const slots, trait obligations,
  type/value equalities, выбранный solver impl и агрегат операторной
  семантики. Индивидуальных ambiguous candidates в ответе нет.
- Candidate assembly, evaluator и ответ используют нативный `SolverImpl`;
  legacy-представления `ImplRef` и преобразований в него нет.
- `Context::applySolverResponse(const SolverResponse&)` — единственная точка
  применения inference-эффектов к caller table.
- Coercion/unsize передаются в `TraitGoalQuery` как данные relation. Их
  проверка, сравнение endpoints и ranking выполняются внутри солвера; callback
  в `Context` и post-solver retry удалены.
- Кэш хранит полный неизменяемый canonical response. One-shot response,
  `slotsBefore` и мутация caller inference на границе удалены.

## Проекции и выбор

- `NormalizesTo(<T as Trait>::Assoc, ?out)` возвращает associated output и все
  inference-эффекты одним типизированным ответом.
- Dynamic/static EAT только ставят цель и применяют ответ.
- Trait impl, ParamEnv, builtin, trait-object и opaque heads проходят общую
  транзакционную relation. Pending alias relations становятся nested goals.
- Specialization, associated item source, inherent impl и method selection
  решаются до выхода из solver/selection слоя.
- Отдельные EAT-селекторы, recursion state, `definingUse`, `selfSimilarChain`,
  `noGoalBridge` и consumer fallback удалены.

## Потребители

На solve/apply переведены expression typeck, static resolve, method lookup,
autoderef и builtin-доказательства, включая `Deref`, `Unsize` и
`CoerceUnsized`. Static bridge передаёт полный `SolverResponse`; `canUnsize`
ставит обычную цель `Unsize<dst>` и принимает только `Proven`.

Identity retry, specialisable repeat, операторный legacy probe, trait-driven
possibilities и ручной выбор единственного ambiguous candidate удалены.
Поздний output diverging closure передаётся явной obligation, без мутации HIR
через `const_cast`.

Низкоуровневый `HIRTraitImplCallback` в `HIRCrate` только перечисляет HIR
declarations для candidate assembly. Он не является границей solver API, не
несёт `HIRCompare` и не выбирает ответ.

## Gates

- C++ UT компиляцией фиксирует точную форму `SolverResponse` и
  `TraitGoalQuery`, а также единственный overload
  `Context::applySolverResponse`. Возврат candidate export или старого apply
  API ломает сборку, без парсинга исходников тестом.
- Семантические Rust-регрессии покрывают ambiguity, ParamEnv, projections,
  coercion/unsize, operators, inherent methods и static consumers.
- Итоговый полный Nix `unit` на рабочем дереве зелёный.

История миграции, аудит преждевременного «готово» и закрытие каждого найденного
моста находятся в `SOLVER_EX.md`.
