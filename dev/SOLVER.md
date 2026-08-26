# SOLVER.md — удалить legacy/fuzzy пути

Цель: typeck пользуется одним goal solver. Снаружи солвера нет выбора
impl'а по `HIRCompare::Fuzzy`, повторного legacy-поиска и угадывания типа по
списку possibilities.

`Fuzzy` допустим только как внутренний результат сопоставления головы
кандидата. Через границу солвера выходят `NoSolution`, `Ambiguous` или
`Proven` и ограничения ответа.

## 1. Нормальный ответ солвера

- Канонизировать все type/const inference variables входной цели.
- Каждый кандидат проверять в изолированном inference state.
- Ответ хранит certainty и значения всех канонических входов; созданные при
  решении переменные остаются каноническими existential variables.
- Применять ответ к caller inference обычной унификацией.
- Несколько кандидатов: одинаковые ответы объединяются, разные дают
  ambiguity без ограничений.
- Кэш хранит полный канонический ответ; удалить `slotsBefore`, one-shot
  response и `const_cast`/`setIvarTo` на границе солвера.

Готово, когда single-candidate inference работает только через применение
ответа, без повторного поиска в typeck.

## 2. Проекции решает солвер

- Ввести цель `NormalizesTo(<T as Trait>::Assoc, ?out)`; `?out` является
  входом канонического запроса и связывается ответом.
- `expandAssociatedTypes` только обходит тип и ставит такие цели.
- Selection, specialization, associated item и builtin-кандидаты выбираются
  внутри solver candidate assembly.
- Циклы проекций обрабатываются solver table/fixpoint, а не depth cap и не
  `definingUse`/`selfSimilarChain`.
- После перевода удалить fallback из EAT в `findTraitImplsMagic` и legacy EAT.

Готово, когда нормализация не имеет собственного выбора best/fuzzy impl'а.

Текущий срез:

- `NormalizesTo` возвращает неоднозначные nested goals выбранного кандидата
  вызывающей стороне; typeck регистрирует их как obligations.
- Локальный `const_cast`/`setIvarTo` из EAT удалён; прямые входные type-var
  применяются на границе ответа нормализации.
- Осталось заменить это полным canonical response (`var_values` + external
  constraints) и хранить его целиком в solver cache.

## 3. Перевести всех потребителей

По одному, с отдельным regression-тестом:

1. trait/associated constraints expression typeck;
2. static resolve и проверка bounds;
3. method probe/confirm и autoderef;
4. `Deref`, `Unsize`, `CoerceUnsized` и остальные builtin-доказательства.

Каждый потребитель вызывает один solver API и применяет его ответ. Удалять
сразу после перевода соответствующие identity-retry, possibilities-probe и
specialisable-repeat, не оставляя запасной путь.

## 4. Удалить старую механику

- Удалить публичные `findTraitImplsLegacy*` и `findTraitImplsMagic*`.
- Внутренние перечислители impl/builtin/ParamEnv оставить только как части
  candidate assembly, без отдельной семантики выбора.
- Удалить `selectSpecialisableFallback`.
- Удалить trait possibilities из `possibleIvarVals` и все уровни
  `checkIvarPoss`, выбирающие тип по bounds. Coercion graph оставить отдельным
  направленным механизмом, не частью trait solving.
- Удалить передачу `HIRCompare::Fuzzy` из солвера в typeck callbacks.

## Definition of done

- В typeck нет вызовов `findTraitImplsLegacy*`/`findTraitImplsMagic*`.
- Нет retry после ответа goal solver и нет выбора типа из trait
  possibilities.
- Проекции нормализуются только через `NormalizesTo`.
- `unit` зелёный; полный корпус и perf запускаются на границе каждого из
  четырёх пунктов.

Рабочий цикл: сначала красный unit-тест, зелёный upstream rustc 1.90, затем
фикс. Одна законченная возможность — один commit и push. Пройденные пункты
вычёркивать здесь.
