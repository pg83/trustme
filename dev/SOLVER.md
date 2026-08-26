# SOLVER.md — план доведения typeck/солвера до rustc-семантики

Снимок 2026-08-26: goal solver — единственный (legacy-селекция выпилена,
c7299e188), throw Defer не существует (15173547a), корпус — 8 узлов,
libcore full pipeline 41-44s. Всё недоделанное по семантике собрано
ниже этапами с якорями по живому коду: каждый пункт долга лежит в
этапе, который его убивает.

Дисциплина каждого этапа: unit-гейт, полный корпус, libcore-время не
хуже 41-44s, commit+push, запись в DEFER.md. Порядок этапов задан
семантическими зависимостями.

Не трогаем перечислители Magic/Legacy/Bound внутри СБОРКИ КАНДИДАТОВ
солвера (helpers:3853-3855, CandidateSource): это источники кандидатов.
Удаляются их вызовы из typeck и отдельная семантика вокруг них.

Вне скоупа: регионы. Семантика корректной программы region-erased;
borrowck/region obligations только отвергают некорректные программы
(borrowck-only диагностики — xfail по договорённости). Исключение —
leak check при HR-матчинге, потому что он влияет на выбор impl'а; он в
этапе 8.

## Этап 0. Канонический ответ с ограничениями

Долг: солвер на цель с иварами, через которые не коммитит, отвечает
forced-ambiguity identity (`emitForcedAmbiguity`; ordinary lookup без
assocName ответа не получает). Вывод затем восстанавливают fuzzy-
обходы. Response-кэш one-shot, а `slotsBefore` запрещает ответу
содержать ивары, не перечисленные во входе.

Сделать:

- каждый кандидат проверяется изолированно и возвращает связывания
  канонических слотов цели, новые obligations и certainty, не меняя
  caller inference;
- ответы применимых кандидатов объединяются: при неоднозначности
  сохраняются только общие для них ограничения;
- деканонизация применяет ответ через обычную семантическую
  унификацию типов/значений, а не через `setIvarTo` и не через
  `const_cast`;
- канонические слоты с самого начала различают type и const ивары и
  имеют поле universe (пока всегда 0);
- свободные generic-параметры различаются по owner/binder либо
  канонизируются как универсальные слоты: M:0 разных функций не
  должен быть одним ключом;
- предусмотреть в ответе отдельный канал внешних ограничений для
  hidden types; заполнение этого канала — этап 7.

Убивает:

- identity как способ кодировать ambiguity;
- identity-retry (expr_cs:8181-8183);
- probe-обходы для possibilities (expr_cs:7783-7785, 9029-9031);
- `slotsBefore` в `emitResponse`;
- сброс response-кэша на каждом outermost-входе и one-shot ответы;
- точечные `const_cast<HMTypeInferrence>` на границе солвера.

Корпус-ожидание: rust_lib «Failed to infer» ×3 (4fcc, 66549, ed05);
вероятно exercism-timeout за счёт исчезновения повторных обходов.

## Этап 1. Единая селекция: specialization и builtin-кандидаты

Долг: candidate assembly уже видит ParamEnv, crate impls и
`CandidateSource::Builtin`, а response-путь частично делает
`moreSpecificThan`. Но trait-only мост сливает default impl и
перекрывающий impl в одинаковый ответ, после чего typeck повторяет
legacy lookup. Часть builtin-правил по-прежнему вызывается напрямую
из typeck/static resolve. `MetaSized` — TODO (helpers:2180), а
`?Sized` живёт вне env-модели.

Сделать:

- specialization и наследование итема завершаются внутри селекции;
  запрос, которому нужен impl/итем, получает выбранный impl и цепочку
  shadowed-предков;
- builtin-правила становятся обычными кандидатами с общей семантикой
  applicability, ambiguity и отрицательных impl'ов;
- реализовать MetaSized, перенести `?Sized` в env-модель;
- coherence-режим не применяет обычное preference-отсечение там, где
  необходимо учитывать все потенциально пересекающиеся кандидаты.

Убивает:

- noGoalBridge-двухпроходность (static:2546, 4258, 4272);
- specialisable-repeat (expr_cs:8211-8213) и
  `selectSpecialisableFallback`;
- прямые вызовы Magic/Legacy из typeck;
- `magicTraitImpls` и кэш-исключение Sized-семейства.

Зависимость: этап 0. Этот этап идёт до NormalizesTo, потому что
значение ассоциированного типа зависит от выбранного specialized
impl'а.

## Этап 2. NormalizesTo и семантика циклов

Долг: `expandAssociatedTypes` — отдельная машинерия с bestImpl/fuzzy-
сравнениями, `definingUse`/`selfSimilarChain`-гейтами и depth-caps
`>64`. Dynamic и static EAT имеют собственные стеки и кэши. Текущий
goal cycle отвечает «coinductive → Proven, иначе Ambiguous» без
fixpoint и без зависимости результата от пути цикла.

Сделать:

- добавить цель `<T as Trait>::Assoc = ?out`;
- внешний структурный EAT только обходит тип и ставит NormalizesTo-
  цели; внутренний normalization солвера не вызывает внешний EAT;
- различать индуктивные и коиндуктивные пути, хранить provisional
  ответы и итерировать их до fixpoint;
- сначала перевести dynamic EAT, затем static EAT;
- удалить произвольные depth-caps, `definingUse`/`selfSimilarChain`-
  ограды и mutable static recursion stack после перевода их
  потребителей;
- структурный fast-path допустим только как оптимизация, выдающая тот
  же результат, что полная селекция, а не как второй источник
  семантики.

Зависимости: этап 0 даёт биндинг `?out`, этап 1 определяет impl и
унаследованный associated item. NormalizesTo и fixpoint cycles
сдаются одним этапом: рекурсивная проекция без cycle semantics
некорректна.

## Этап 3. Удаление trait-based inference guesses

Долг: `possibleIvarVals` смешивает три разных механизма: возможные
типы из trait bounds, generic defaults и направленный граф coercions.
Четыре яруса `checkIvarPoss`
(Assume/IgnoreWeakDisable/PickFirstBound/FinalOption) выбирают тип
эвристически. Безусловный числовой fallback (73a800d7f) — временная
ступень.

Сделать:

- single-candidate commit получает связывания из ответа этапа 0;
- последовательно удалить PickFirstBound/FinalOption, затем
  Assume/IgnoreWeakDisable, проверяя корпус после каждого шага;
- literal/generic defaults применяются, после чего связанные цели
  решаются заново;
- удалить из `possibleIvarVals` trait possibilities, но пока оставить
  coercion-рёбра; окончательно структура удаляется в этапе 4.

Корпус-ожидание: часть simd/array-type и rust_lib inference failures.

## Этап 4. Coercions, never и autoderef

Долг: linkCoerce/checkCoerceTys/checkIvarPossFailsBounds/autoderef —
собственный fuzzy-механизм. CoerceUnsized пробуется отдельно
(expr_cs:6737), autoderef над defining opaque отвечает Ambiguous.
Never type обрабатывается точечными заплатками: Diverge в матчинге
ассоциатов принудительно Ambiguous (helpers:4251, 4850).

Сделать:

- оставить coercion отдельным направленным механизмом, который
  выбирает преобразование и записывает adjustment;
- доказательства Deref, Unsize и CoerceUnsized получать из солвера;
- NeverToAny выражать как coercion adjustment, не как унификацию
  ивара с `!`;
- после переноса coercion-рёбер удалить остаток `possibleIvarVals`;
- убрать Diverge-заплатки и завершить diverging fallback.

Зависимости: этап 0 — связывания, этап 1 — builtin Unsize/
CoerceUnsized, этап 2 — `Deref::Target`, этап 3 — удалённые trait
guesses.

Корпус-ожидание: оставшаяся часть simd/array-type.

## Этап 5. Клоужеры и async

Долг: expected signature клоужера (expr_cs:7258+) и
Fn/FnMut/FnOnce kind выводятся легаси-механикой; AsyncFn→Fn
форвардинг closure-структур — хак в `findTraitImplsTypesCb`.

Сделать:

- expected Fn-family obligation задаёт сигнатуру клоужера;
- фактический Fn/FnMut/FnOnce kind определяется использованием
  захватов и затем проверяется против obligation, а не выбирается по
  требуемому трейту;
- async-структуры дают собственные AsyncFn-кандидаты вместо
  форвардинга.

Зависимости: этап 0, этап 4 для coercions сигнатур.

## Этап 6. Method probe и confirm

Долг: пробинг и where-bound shadowing (`foundNonGlobalBound`) —
эвристическая смесь перечисления, унификации и коммита.

Сделать:

- probe перечисляет receiver adjustments и кандидатов без изменения
  caller inference;
- obligations каждого кандидата проверяет солвер;
- probe возвращает стабильный pick без живых иваров;
- confirm применяет substitutions, obligations и adjustments.

Метод не становится goal целиком: солвер отвечает только за
применимость trait/Deref-кандидатов. Зависимости: этап 0 и этап 4.

## Этап 7. Опаки: TypingMode и RPITIT

Долг: analysis-режим — набор точечных правил (definingFcnOrigins,
rigid вне defining function, defining-use у constraint loop) вместо
hidden-type кандидатов в инференс-таблице. RPITIT живёт на строковых
именах `erased#<item>_<i>` (hir_from_ast:1081, hir_expand:6587,
NotYetKnown-ветка revealOpaqueType). `revealOpaqueType` лениво
генерирует MIR чужого итема ради erasedTypes; trans дополнительно
reveal'ит типы в `TypeVisitor::visitType` и `TargetGetTypeRepr`.
RPIT-only-self имеет отдельный fallback в `()`.

Сделать:

- TypingMode {Analysis(defining scope), PostAnalysis};
- hidden-type кандидаты и member constraints хранятся в inference и
  выходят из солвера через канал внешних ограничений этапа 0;
- RPITIT представляется структурными синтетическими ассоциатами;
- erasedTypes предвычисляются на границе фаз; `getOrGenMir` уходит из
  reveal;
- снять лишние reveal-заходы в trans и RPIT fallback после перевода.

## Этап 8. Universes/HRTB и leak check

Долг: placeholder-генерики (`GENERICPlaceholder`) принудительно дают
Ambiguous вместо universe-семантики. Leak check отсутствует, хотя HR
fn-ptr и `'static` impl могут из-за этого выбрать разные ответы.
nested-hkl падает с Cyclic anon type.

Сделать: использовать поле universe канонических слотов этапа 0,
создавать placeholders в своём universe и проверять утечку при
матчинге.

Корпус-ожидание: nested-hkl (728335c8633b).

## Этап 9. WF и implied bounds

Долг: `selectWellFormed` — узкий обход проекций; полнота ParamEnv и
applicability кандидатов зависит от отдельно собранных implied
bounds.

Сделать: WellFormed obligations и полный набор implied bounds,
используемый одинаково обычной селекцией, method probe и coherence.
Форму WellFormed-цели можно завести в этапе 0, семантика закрывается
здесь.

## Этап 10. Coherence: финальная сверка intercrate

Долг: coherenceMode, `traitRefIsKnowable` и positive/negative
auto-impls есть (helpers:3477, 3871, 4666+), но intercrate ambiguity,
negative reasoning и orphan-полнота системно не сверялись.

Направленные characterization-тесты добавляются с ранних этапов.
Финальный аудит делается здесь: он зависит от specialization/builtin
семантики этапа 1, cycles этапа 2, universes этапа 8 и полного ParamEnv
этапа 9.

## Этап 11. Const traits и const inference

- Const traits (`[const]`) в солвере.
- Символическая оценка констант под where вместо
  `constItemMustStaySymbolic`.
- Value-Infer и unevaluated consts в canonical keys и response cache;
  базовый тип const-слота уже существует с этапа 0.

После стабилизации этапов 0-10.

## Вне плана (не солвер)

- rust_lib 297ac (`mem::test_transmute_copy`, runtime) — trans/layout,
  отдельный разбор.
- Зачистка мутабельных статиков со стейтом (стеки рекурсии
  resolve/hir_type, repr-кэши trans_target на std::unordered_map,
  счётчики имён, sActiveDiscriminants, nextAliasInputInfer, скретч
  helpers:392) — отдельный проход по канону пул/резолвер/wire board.
