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
