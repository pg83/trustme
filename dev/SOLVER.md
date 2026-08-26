# Семантическая карта недоделанного: typeck и goal solver

Снимок на 2026-08-26 (после выпила legacy-селекции, c7299e188).
Дефолт — goal solver; ниже — что в нём и вокруг него семантически
не доведено до rustc-модели, по весу.

## Ядро солвера

### 1. Нормализация (EAT) не через солвер

`expandAssociatedTypes` — отдельная машинерия с `bestImpl`/fuzzy-
сравнениями. Goal-путь внутри неё частичен и огорожен эвристиками:
`definingUse`-гейт, `selfSimilarChain`-гейт, depth-caps `>64` (числа с
потолка, введены при отладке зависаний). В rustc next-solver
нормализация — это `NormalizesTo`/`AliasRelate` цели ВНУТРИ солвера, с
теми же кэшами и циклной семантикой. У нас два мира и мосты между ними.

### 2. Ответы не каноничны по-настоящему

Цель с иварами, через которые солвер не коммитит, отвечается
forced-ambiguity identity (`emitForcedAmbiguity`; недоступен ordinary
lookup без assocName). Вывод дальше тянут legacy fuzzy-обход +
single-candidate commit из `possibleIvarVals`
(identity-retry в `hir_typeck_expr_cs.cpp`, check_associated). rustc
возвращает канонический ответ со связываниями иваров. Пока это так,
«выпил legacy» неполон принципиально: fuzzy-обходы — костыль вместо
canonical response inference.

### 3. Специализация не смоделирована

Мост сворачивает default-impl и перекрывающий его sibling в ОДИН
merged-ответ; наследование итемов и `moreSpecificThan` живут снаружи
солвера (двухпроходность `noGoalBridge`, specialisable-повтор в
`hir_typeck_static.cpp` и expr_cs). В rustc — candidate preference
внутри селекции.

### 4. Циклы — эвристика

«Productive recursive proves provisional, ordinary cycle → Ambiguous» —
приближение. rustc: точная coinductive/inductive семантика (auto
traits/Sized коиндуктивны) с fixpoint-итерацией провизорных
результатов. Наши active-goal циклы индуктивность не различают.

### 5. Канонизация не context-free

Generic-биндинги сравниваются только по binding — `M:0` разных функций
совпадают. Отсюда ПОЛНЫЙ флаш warm-кэша на смене eatCacheGeneration
вместо честных ключей. Симптом: канонические ключи не абсолютны.

### 6. Нет universes/HRTB

Placeholder-генерики (`GENERICPlaceholder`) с правилами принудительной
Ambiguous вместо universe-семантики и leak check. Higher-ranked цели
матчатся приближённо; nested-hkl в корпусе падает (Cyclic anon type).

## Typeck вокруг солвера

### 7. Магические трейты вне солвера

Sized/Copy/Clone/FnPtr/Transmute/DiscriminantKind — структурные ответы
в `findTraitImplsMagicCb` ДО goal-машинерии; в канонических
кэшах/циклах не участвуют единообразно. `MetaSized` — буквально
`TODO(sp)` (`hir_typeck_helpers.cpp:2180`). `?Sized`-декларация
параметра — контекст вне env-модели (исключение Sized-семейства из
крейт-кэша для generic-целей — обход, не модель).

### 8. Опаки: нет TypingMode

Для trans есть `OpaqueReveal::All` (свойство резолвера, 59a9e999b), но
analysis-режим — набор точечных правил (definingFcnOrigins, rigid вне
defining function, autoderef над defining-опаком → Ambiguous,
defining-use у constraint loop), а не rustc-овское хранение hidden type
candidates в infcx с member constraints.

### 9. Литеральный вывод — легаси-пласт целиком

`possibleIvarVals` + четыре яруса `checkIvarPoss`
(Assume/IgnoreWeakDisable/PickFirstBound/FinalOption) + безусловный
числовой fallback последней стадией (73a800d7f) — эвристики. rustc:
fallback через анализ diverging-переменных и coercion-графа.
simd/array-type (вывод intrinsic-аргументов) падает именно тут.

### 10. Method probe не через солвер

Пробинг и shadowing where-bound против крейт-impl'ов
(`foundNonGlobalBound`) — аппроксимация rustc candidate ordering.

### 11. WF и implied bounds поверхностны

`selectWellFormed` — узкий обход проекций; полных WF-обязательств и
implied bounds нет.

### 12. Const-generics частичны

Value-Infer ключи в кэшах certainty-only; unevaluated consts блокируют
кэширование; `constItemMustStaySymbolic` — предикат-эвристика вместо
символической оценки под where-clauses; const traits (`[const]`) в
солвере не смоделированы.

### 13. Регионов нет вообще

Borrowck отсутствует (borrowck-only диагностики — xfail по
договорённости), но это шире: no region obligations, no leak check —
часть trait-семантики rustc, зависящая от регионов, не выражается.

## Приоритет

П.1-2 (нормализация и канонические ответы в солвере) — они же убьют
fuzzy-костыли и половину гейтов. Затем циклы (п.4) и специализация
(п.3). Корпусный остаток (rust_lib «Failed to infer» ×3) почти
наверняка упирается в п.2/п.9.

# План: удаление fuzzy/legacy путей и реализация недостающего

Порядок не произвольный: этап 0 разблокирует 1, 2 и 5; без него любые
выпилы fuzzy-обходов ломают вывод. Дисциплина на каждом этапе: unit-гейт,
полный корпус, libcore-время (не хуже 41-44s), commit+push, запись в
DEFER.md. Легитимные перечислители в СБОРКЕ КАНДИДАТОВ солвера
(helpers:3853-3855 — Magic/Legacy/Bound как CandidateSource) не трогаем:
это источники кандидатов, а не костыли; костыли — их вызовы ИЗ typeck.

## Этап 0. Канонические ответы со связываниями иваров

Солвер на цель с иварами отвечает подстановками для канонических слотов,
декануниазация применяет их к caller-иварам. Обобщить существующий
точечный коммит «параметров-констрейнтов на голые ивары» (const_cast +
setIvarTo в globally-EAT) в единый механизм применения ответа — без
const_cast, через явный мутабельный канал к таблице иваров.

Убивает:
- emitForcedAmbiguity identity как способ ответа (остаётся только как
  честная ambiguity без кандидатов);
- identity-retry fuzzy-обход (expr_cs:8181-8183);
- probe-обходы для possibilities (expr_cs:7783-7785, 9029-9031).

Ожидание по корпусу: rust_lib «Failed to infer» ×3 (4fcc, 66549, ed05).
Выход: перечисленные сайты удалены, гейт+корпус зелёные.

## Этап 1. NormalizesTo: EAT через солвер

Новый вид цели — нормализация алиаса (`<T as Trait>::Assoc = ?out`).
EAT-globally-ветка превращается в вызов солвера; структурный fast-path
(конкретный self, единственный impl) остаётся вне для скорости.

Убивает: definingUse-гейт, selfSimilarChain-гейт, depth-caps >64
(циклы переходят к солверу), отдельный goal-канал внутри EAT.
Зависимость: этап 0 (результат нормализации — биндинг выходного ивара).
Побочный эффект: канонические ключи становятся абсолютными → снять
полный флаш warm-кэша на eatCacheGeneration (долг п.5) и переоценить
крейт-кэш для generic-целей (сейчас — без измеримого прироста).

## Этап 2. Специализация в селекции

Candidate preference внутри солвера: более специфичный impl побеждает до
merge; ответ несёт выбранный impl и его shadowed-предка (для
наследования итемов) вместо повторного перечисления.

Убивает: noGoalBridge-двухпроходность (static:2546, 4258, 4272),
specialisable-repeat (expr_cs:8211-8213), selectSpecialisableFallback.
Зависимость: этап 1 (lookup итема через нормализацию).

## Этап 3. Семантика циклов

Коиндуктивные цели (auto traits, Sized-семейство) vs индуктивные;
fixpoint-итерация провизорных результатов вместо эвристики
«productive proves provisional». Делать вместе с этапом 1 —
normalizes-to добавляет новые классы циклов.

## Этап 4. Builtin-кандидаты вместо magic-претрапа

Sized/Copy/Clone/FnPtr/Transmute/DiscriminantKind — builtin-кандидаты
сборки (канал CandidateSource::Builtin уже есть). Реализовать MetaSized
(TODO helpers:2180). ?Sized-декларации параметров — в env-модель;
снять исключение Sized-семейства из крейт-кэша.

Убивает: findTraitImplsMagicCb-претрап до солвера, magicTraitImpls-флаг
маршрутизации и с ним последний legacy-маршрут в findTraitImplsCb.

## Этап 5. Литеральный вывод

После этапа 0 single-candidate commit — солверный (биндинги
единственного кандидата). Ярусы checkIvarPoss срезать поэтапно с
корпусом между шагами: сначала PickFirstBound/FinalOption, затем
Assume/IgnoreWeakDisable; числовой fallback остаётся стадией
«применить дефолты → пере-решить отложенные цели». Конечная цель —
possibleIvarVals удалён целиком, CoerceUnsized-проба (expr_cs:6737)
уходит в солверные Unsize-цели.

Ожидание по корпусу: simd/array-type, i32-fallback-класс закрыт
системно (сегодняшний безусловный fallback — временная ступень).

## Этап 6. Method probe через солвер

Кандидаты метода формулируются целями; порядок и where-bound shadowing
по rustc candidate ordering. Убивает foundNonGlobalBound-эвристику.

## Этап 7. TypingMode для опаков

Hidden-type кандидаты хранятся в инференс-таблице; defining-scope —
режим тайпчека; member constraints. OpaqueReveal::All становится
TypingMode::PostAnalysis. Убивает definingFcnOrigins-правила и
autoderef-заглушку (defining-опак → Ambiguous).

## Этап 8. Universes/HRTB + leak check

Канонизация с универсами; GENERICPlaceholder → placeholder с универсом;
forced-ambiguity правила для плейсхолдеров умирают.
Ожидание по корпусу: nested-hkl (Cyclic anon type).

## Этап 9. Хвост

WF/implied bounds, const traits в солвере, region obligations —
после стабилизации этапов 0-8, отдельными заходами.
