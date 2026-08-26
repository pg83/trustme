# SOLVER.md — план доведения typeck/солвера до rustc-модели

Снимок 2026-08-26: goal solver — единственный (legacy-селекция выпилена,
c7299e188), throw Defer не существует (15173547a), корпус — 8 узлов,
libcore full pipeline 41-44s. Всё недоделанное по семантике собрано
ниже — не отдельной «картой», а этапами с якорями по живому коду:
каждый пункт долга лежит в этапе, который его убивает.

Дисциплина каждого этапа: unit-гейт, полный корпус, libcore-время не
хуже 41-44s, commit+push, запись в DEFER.md. Порядок этапов —
зависимость, не приоритет вкуса: этап 0 разблокирует почти всё.

Не трогаем: перечислители Magic/Legacy/Bound внутри СБОРКИ КАНДИДАТОВ
солвера (helpers:3853-3855, CandidateSource) — это источники
кандидатов; костыли — их вызовы ИЗ typeck, они и выпиливаются.

Вне скоупа: регионы. Семантика корректной программы region-erased;
borrowck/region obligations только отвергают некорректные программы
(borrowck-only диагностики — xfail по договорённости). Единственное
регион-зависимое, влияющее на выбор impl'а в корректных программах, —
leak check при HR-матчинге; он в этапе 10.

## Этап 0. Канонические ответы со связываниями иваров

Долг: солвер на цель с иварами, через которые не коммитит, отвечает
forced-ambiguity identity (`emitForcedAmbiguity`; недоступен ordinary
lookup без assocName), и вывод дальше тянут fuzzy-обходы. rustc
возвращает канонический ответ с подстановками.

Сделать: ответ несёт подстановки для канонических слотов цели;
деканонизация применяет их к caller-иварам. Обобщить точечный
const_cast+setIvarTo-коммит «голых иваров» из globally-EAT в единый
механизм применения ответа — без const_cast, через явный мутабельный
канал к таблице иваров.

Убивает:
- identity как способ ответа (остаётся честная ambiguity без кандидатов);
- identity-retry fuzzy-обход (expr_cs:8181-8183);
- probe-обходы для possibilities (expr_cs:7783-7785, 9029-9031);
- slot-count гард в emitResponse («ответ ивар-цели без слотов не
  кэшировать») — заплатка этого же корня.

Корпус-ожидание: rust_lib «Failed to infer» ×3 (4fcc, 66549, ed05);
вероятно exercism-timeout (перф от исчезновения ретраев).

## Этап 1. NormalizesTo: EAT через солвер

Долг: `expandAssociatedTypes` — отдельная машинерия с bestImpl/fuzzy-
сравнениями; goal-канал внутри неё огорожен `definingUse`-гейтом,
`selfSimilarChain`-гейтом и depth-caps `>64` (числа с потолка). Два
мира и мосты. Канонические ключи не абсолютны (generic-биндинги
сравниваются только по binding → M:0 разных функций совпадают), отсюда
полный флаш warm-кэша на eatCacheGeneration и one-shot ответы (сброс
response-кэша на входе outermost — маскирует невалидность реплея).

Сделать: цель-нормализация алиаса (`<T as Trait>::Assoc = ?out`)
внутри солвера; EAT-globally-ветка — вызов солвера; структурный
fast-path (конкретный self, единственный impl) остаётся для скорости.
Ключи каноничны абсолютно → флаш и one-shot умирают, реплей ответов
внутри функции легален; переоценить крейт-кэш generic-целей (сейчас
допущены, прироста нет — 9039f32b2).

Зависимость: этап 0 (результат нормализации — биндинг выходного ивара).
Делать вместе с этапом 2 (циклы normalizes-to).

## Этап 2. Семантика циклов

Долг: «productive recursive proves provisional, ordinary cycle →
Ambiguous» — эвристика; индуктивность не различается.

Сделать: коиндуктивные цели (auto traits, Sized-семейство) vs
индуктивные; fixpoint-итерация провизорных результатов. Вместе с
этапом 1 — normalizes-to добавляет новые классы циклов.

## Этап 3. Специализация в селекции

Долг: мост сворачивает default-impl и перекрывающий sibling в один
merged-ответ; `moreSpecificThan` и наследование итемов — снаружи.

Сделать: candidate preference внутри солвера; ответ несёт выбранный
impl и shadowed-предка (для наследования итемов).

Убивает: noGoalBridge-двухпроходность (static:2546, 4258, 4272),
specialisable-repeat (expr_cs:8211-8213), selectSpecialisableFallback.
Зависимость: этап 1.

## Этап 4. Builtin-кандидаты и Sized-иерархия

Долг: Sized/Copy/Clone/FnPtr/Transmute/DiscriminantKind отвечаются в
`findTraitImplsMagicCb` ДО goal-машинерии — вне канонических
кэшей/циклов. `MetaSized` — TODO (helpers:2180). `?Sized`-декларация
параметра — контекст вне env-модели (крейт-кэш исключает
Sized-семейство для generic-целей как обход).

Сделать: перенести структурные правила в builtin-кандидаты сборки
(канал CandidateSource::Builtin есть); реализовать MetaSized;
?Sized — в env-модель.

Убивает: magic-претрап, флаг magicTraitImpls и с ним последний
legacy-маршрут в findTraitImplsCb; кэш-исключение Sized-семейства.

## Этап 5. Литеральный и never вывод

Долг: `possibleIvarVals` + четыре яруса `checkIvarPoss`
(Assume/IgnoreWeakDisable/PickFirstBound/FinalOption) + безусловный
числовой fallback (73a800d7f — временная ступень) — эвристики вместо
rustc-анализа diverging-переменных. Never type — точечные заплатки:
Diverge в матчинге ассоциатов принудительно Ambiguous (helpers:4251,
4850), RPIT-only-self фолбэк в `()` (fallbackUnresolvedRpitType).
simd/array-type падает здесь (вывод intrinsic-аргументов).

Сделать: после этапа 0 single-candidate commit — солверный (биндинги
единственного кандидата); ярусы срезать поэтапно с корпусом между
шагами (сначала PickFirstBound/FinalOption, затем
Assume/IgnoreWeakDisable); fallback = «применить дефолты → пере-решить
цели», never-fallback как diverging-класс, Diverge-заплатки убрать.

Конечная цель: possibleIvarVals удалён целиком.
Корпус-ожидание: simd/array-type.

## Этап 6. Коэрции и autoderef через солвер

Долг: linkCoerce/checkCoerceTys/checkIvarPossFailsBounds/autoderef —
собственный fuzzy-механизм унификации с коэрциями; CoerceUnsized-проба
(expr_cs:6737); autoderef над defining-опаком → Ambiguous (заглушка).
rustc-коэрции опираются на солверные Unsize/CoerceUnsized цели.

Сделать: Unsize/CoerceUnsized как цели солвера; coercion-граф
переиспользует ответы со связываниями (этап 0); autoderef-шаг задаёт
Deref-цели.

Зависимость: этапы 0 и 5.

## Этап 7. Клоужеры и async

Долг: вывод сигнатуры клоужера из expected type (expr_cs:7258+) и
Fn/FnMut/FnOnce kind — легаси-механика; AsyncFn→Fn форвардинг
closure-структур — хак в findTraitImplsTypesCb.

Сделать: сигнатура/kind выводятся через obligations (Fn-family цели с
ивар-сигнатурой; kind — по требуемому трейту); async-структуры дают
честные AsyncFn-кандидаты вместо форвардинга.

Зависимость: этап 6 (коэрции сигнатур), этап 0.

## Этап 8. Method probe через солвер

Долг: пробинг и where-bound shadowing (`foundNonGlobalBound`) —
аппроксимация rustc candidate ordering.

Сделать: кандидаты метода — цели; порядок и shadowing по rustc.
Зависимость: этап 0; лучше после 6 (receiver-коэрции).

## Этап 9. Опаки: TypingMode и RPITIT

Долг: analysis-режим — набор точечных правил (definingFcnOrigins,
rigid вне defining function, defining-use у constraint loop) вместо
хранения hidden-type кандидатов в инференс-таблице с member
constraints. RPITIT живёт на строковых именах `erased#<item>_<i>`
(hir_from_ast:1081, hir_expand:6587, NotYetKnown-ветка
revealOpaqueType) — конвенция, не модель. Фазовая перепутанность:
revealOpaqueType лениво генерит MIR чужого итема ради erasedTypes —
транс тянет typeck-лоуринг. Плюс двойная страховка в trans:
TypeVisitor::visitType и вход TargetGetTypeRepr reveal'ят поверх
фикспойнта EAT (59a9e999b) — не проверены на удаляемость.

Сделать: TypingMode {Analysis(defining scope), PostAnalysis} —
OpaqueReveal::All становится частным случаем; hidden-type кандидаты в
таблице; RPITIT — структурные синтетические ассоциаты; erasedTypes
предвычисляются на границе фаз (getOrGenMir из reveal уходит);
страховочные reveal-заходы в trans снять.

## Этап 10. Universes/HRTB и leak check

Долг: placeholder-генерики (GENERICPlaceholder) с принудительной
Ambiguous вместо universe-семантики; leak check отсутствует — а он
влияет на выбор impl'а в корректных программах (HR fn-ptr vs
'static-impl). nested-hkl падает (Cyclic anon type).

Сделать: канонизация с универсами, placeholder с универсом, leak check
в матчинге.

Корпус-ожидание: nested-hkl (728335c8633b).

## Этап 11. Coherence: сверка intercrate

Долг: coherenceMode + traitRefIsKnowable и учёт positive/negative
auto-impl'ов есть (helpers:3477, 3871, 4666+), но полнота
intercrate-семантики (ambiguity чужих крейтов, negative reasoning,
orphan-полнота) против rustc системно не сверялась — только тестами.

Сделать: аудит против rustc-правил + направленные тесты. Независим,
можно в любой момент.

## Этап 12. Хвост

- WF-обязательства и implied bounds (`selectWellFormed` — узкий обход
  проекций).
- Const traits (`[const]`) в солвере; символическая оценка констант под
  where вместо предиката `constItemMustStaySymbolic`; value-Infer ключи
  и unevaluated consts в кэшах (сейчас certainty-only/исключены).

После стабилизации этапов 0-11.

## Вне плана (не солвер)

- rust_lib 297ac (`mem::test_transmute_copy`, runtime) — trans/layout,
  отдельный разбор.
- Зачистка мутабельных статиков со стейтом (стеки рекурсии
  resolve/hir_type, repr-кэши trans_target на std::unordered_map,
  счётчики имён, sActiveDiscriminants, nextAliasInputInfer, скретч
  helpers:392) — отдельный проход по канону пул/резолвер/wire board.
