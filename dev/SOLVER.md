# SOLVER.md — единый goal solver

Цель выполнена: typeck пользуется одним goal solver. Снаружи солвера нет
выбора impl'а по `HIRCompare::Fuzzy`, повторного legacy-поиска и угадывания
типа по trait possibilities.

`Fuzzy` допустим только как внутренний результат сопоставления головы
кандидата. Через границу солвера выходят `NoSolution`, `Ambiguous` или
`Proven` и типизированные ограничения ответа.

## 1. Нормальный ответ солвера — сделано

- Все type/const inference variables входной цели канонизируются.
- Кандидаты проверяются в изолированном inference state под snapshot.
- `SolverResponse` хранит certainty, type/const-слоты, equalities, obligations
  и, когда это требуется probe-запросу, неоднозначные candidates.
- `Context::applySolverResponse` применяет ответ к caller inference обычной
  унификацией; typeck не повторяет выбор кандидата.
- Кэш хранит полный неизменяемый canonical response. One-shot response,
  `slotsBefore` и мутация inference на границе удалены.

## 2. Проекции решает солвер — сделано

- `NormalizesTo(<T as Trait>::Assoc, ?out)` возвращает associated output и все
  inference-эффекты одним типизированным ответом.
- Dynamic/static EAT только ставят цель и применяют её ответ.
- Selection, specialization, associated item и builtin-кандидаты выбираются
  внутри solver candidate assembly.
- Отдельные EAT-селекторы, depth/recursion state, `definingUse`,
  `selfSimilarChain`, `noGoalBridge` и legacy fallback удалены.

## 3. Потребители переведены — сделано

На solve/apply переведены:

1. trait/associated constraints expression typeck;
2. static resolve и проверка bounds;
3. method probe/confirm и autoderef;
4. `Deref`, `Unsize`, `CoerceUnsized` и остальные builtin-доказательства.

Identity-retry, specialisable-repeat и операторный legacy probe удалены.
Поздний output diverging closure передаётся явной obligation, без мутации
HIR через `const_cast`.

## 4. Старая механика удалена — сделано

- Публичных `findTraitImplsLegacy*`/`findTraitImplsMagic*` и отдельного static
  selector больше нет.
- Внутреннее перечисление impl/builtin/ParamEnv существует только как часть
  candidate assembly и проверки repertoire.
- `selectSpecialisableFallback` удалён.
- Trait possibilities удалены из `possibleIvarVals`/`checkIvarPoss`.
  Направленный coercion graph оставлен отдельным механизмом.
- `HIRCompare::Fuzzy` не передаётся наружу через solver callbacks.

## Definition of done

- [x] В typeck нет вызовов публичных legacy/magic selector API.
- [x] Нет retry после ответа goal solver и выбора типа из trait possibilities.
- [x] Проекции нормализуются только через `NormalizesTo`.
- [x] Ограничения доказательства переносятся typed response и применяются в
  одной точке.
- [x] Итоговый Nix `unit` зелёный на финальном рабочем дереве: 1004/1004.

Подробная история перехода и регрессий находится в `SOLVER_EX.md`.
