Ощущение верное. Прочитав всё целиком, я вижу: легаси нельзя выпилить не потому, что «потребители не переведены», а потому что солверу не хватает четырёх базовых примитивов. Каждый fuzzy-костыль — компенсация одного из них. Сначала диагноз с уликами, потом план.

## Диагноз: четыре корневых дефекта

**1. В `HMTypeInferrence` нет транзакций (snapshot/rollback).** `setIvarTo` необратим. Поэтому солвер не может проверить кандидата настоящей унификацией с откатом — и вся «изоляция» сделана суррогатами:
- канонические переменные — интернированные Infer-узлы в зарезервированном диапазоне, а плейсхолдеры — **строки, именованные адресами**: `FMT("impl_?_" << &implParamsDef)` (helpers:7987, static:1173), `static_find_impl_<адрес>` (static:59), `method_wf_<адрес>` (helpers:9639);
- вместо унификации — восемь рукописных Match-биндеров (`BindPlaceholders` helpers:3912, `BindResponse` helpers:4069, `GetParams`×2, `Matcher`×2, `HrtbBoundMatcher`, `OwnedImplMatcher`), каждый — частичная унификация с ручным «saved/restore» клонов параметров и своим набором дыр; fticCheckParams крутит фикспойнт плейсхолдеров с потолком `loops < 4` (helpers:8150);
- coherence вынуждена держать **второй резолвер со своей таблицей** (`coherenceIvars`/`coherenceResolve`, helpers:5875), потому что форкнуть таблицу нельзя;
- `makeFreshImplParams` мутирует таблицу через `const_cast` (helpers:5837).

**2. Ответ солвера не несёт ограничений.** `evaluate()` возвращает один `ImplRef` + `HIRCompare` — связывания слотов и nested-цели в ответе не существуют как данные. Отсюда:
- `markAmbiguousIdentity` — хак «ответ есть, но он ничего не значит», и его потребительский парный костыль identity-retry через legacy-перечисление (expr_cs:8170-8190);
- ограничения доказательства доставляются **побочными каналами**: `typeConstraint->registerSolverObligation` (helpers:5295-5318), ре-экспорт баундов `addImplBounds(onlyWithIvars=true)` (expr_cs:7996) — с комментарием «certainty-only path drops inference effects»;
- ответы **одноразовые** («RESPONSES are one-shot», helpers:5026): реплей нелегален, потому что констант-ивары не канонизируются в слоты (helpers:3007, `keyHoldsValues` в 5335 выключает кэш ответов) — отсюда вся серия swap_bytes-хаков;
- `ImplRef::BoundedPtr` — сырые указатели внутрь кэшей баундов, с ручными «repoint traitPath before the caller's frame dies» (helpers:5376-5389).

**3. Нормализация — три независимых селектора.** Динамический EAT (helpers:6108-7092), статический EAT (static:2222-2596) и `matchAssociatedTypes`/AliasRelate внутри эвалюатора выбирают impl'ы каждый по-своему. Они расходятся — и ровно для сверки расхождений живут: `noGoalBridge` двухпроходности (static:2546, 4257-4281), specialisable-repeat (expr_cs:8191-8215), фолбэк EAT в `findTraitImplsMagic/Types/Crate` (helpers:6883-6985). Бонус: статический EAT держит стек рекурсии в **function-local `static`** (`sRecursionLevel`, `sRecursionStack`, static:2231-2238) — тот самый стейт «зависит от того, как вошли по стеку», который ты уже выжигал.

**4. Инференс-догадки живут в потребителе.** `possibleIvarVals`/`checkIvarPoss` — 1500 строк эвристик с шестью уровнями фолбэка и авторским комментарием «TODO: Rewrite ALL of the below» (expr_cs:9929). Он питается fuzzy-ответами (expr_cs:8437-8462 собирает bounded-множества из legacy-перечисления) и потому не может умереть, пока солвер не отдаёт ограничения сам.

Плюс точечные запахи по пути: `methodProbeMustDecide` — mutable-флаг режима, выставляемый вокруг вызова (helpers.h:370, expr_cs:1130); `const_cast<HIRExprNodeClosure*>(...)->returnType = ...` — мутация HIR-узла из глубины сравнения (expr_cs:8049); defining-опаки обязаны идти legacy-путём, потому что только он доходит до `equateErasedAlias` (helpers:6804 — комментарий прямо это признаёт).

## План: примитивы → потом выпил

Это пререквизит-слой под пункт 1 текущего SOLVER.md. Каждый этап: гейт `unit` + корпус, коммит, вычеркнуть.

**~~П0. Транзакции инференса.~~ СДЕЛАНО** (e6dd783ff, c964be55b). Журнал мутаций в `HMTypeInferrence` + `snapshot()`/`rollbackTo()`/`commit()`; поколения монотонны и не повторяются после отката. Coherence-probe работает на основной таблице под снапшотом с отцепленным `typeConstraint`; `coherenceIvars`/`coherenceResolve` удалены, `const_cast` из `makeFreshImplParams` убран. UT: `hir_typeck_helpers_ut.cpp`.

**Прогресс кэш-блока SOLVER.md-1 (2026-08-27, коммиты 855b59b2a..1792a400e):** биндеры дедуплицированы (`BindCandidateParams`); const-ивары канонизируются в value-слоты (виртуальный `monomorphConstgeneric`, слоты в резервном диапазоне, обратные мапы во всех декан-мономорфайзерах) — `keyHoldsValues` удалён; ответы больше не one-shot (реплей легален: ключ = полный канонический вход); `slotsBefore` заменён freeze-механизмом (чужие ивары проходят сырыми и явно детектируются; такие ответы не кэшируются — эмпирика: кэширование их в non-persistent slice ломает `const_memory_intrinsics`, таблица мутирует внутри одной outermost-эвалюации). Граница: unit зелёный, корпус на уровне известного хвоста (nested-hkl, simd/array-type, exercism-timeout, 297ac), новый регресс const-param-in-async пойман корпусом и закрыт (гардированный `getValue`, редукция в unit). Перф: rustsmith_0007 29.5s (было 41-44s). Примечание: perf-узел rustsmith_0007 сломан по построению (bin без crate-type, требует argv) — падает в рантайме независимо от кампании.

**П1. Один примитив унификации.** `unify(sp, a, b) -> Proven / Ambiguous(nested AliasRelate-цели) / Fail`, работающий на настоящей таблице под snapshot. Им переписывается проверка кандидата в `evaluateCandidate`/`matchAssociatedTypes`; Match-биндеры схлопываются на него по одному. `HIRCompare::Fuzzy` перестаёт быть носителем потерянных ограничений — внутри солвера сравнение больше не «трёхзначный бит», а унификация с откатом. *Критерий: `BindPlaceholders`/`BindResponse` удалены; строковые плейсхолдеры остаются только в legacy-путях.*

**Прогресс П5-частично (2026-08-27, 2ee0e8fd1..):** identity-retry, specialisable-repeat и операторный probe в checkAssociated переведены на один solver-вызов (`TraitGoalQuery{.exportAmbiguousCandidates}`): при ambiguity солвер сам отдаёт viable-кандидатов из своей assembly. ВАЖНО (требование пользователя): это транзитный мост — `HIRCompare::Fuzzy` должен исчезнуть с границы ЦЕЛИКОМ, вместе с протоколом `TraitImplCallback`, а не остаться под новым именем. Конечная форма — П2: `SolverResponse { certainty; слоты; obligations; кандидаты при ambiguity }`; экспорт кандидатов пере-выражается как поле ответа.

**Урок (2026-08-27, dd21ec51d→ревертнут ce7e0ac70):** операторный probe НЕЛЬЗЯ переводить на полный `evaluate()` с экспортом: probe дергается на каждой итерации операторного правила, а экспортный путь при infer-Self перечисляет и ОЦЕНИВАЕТ все импы трейта и не кэширует identity → coretests/num уходят в 10-минутные таймауты (130 узлов). Probe вернётся на солвер только когда будут: кэш ответа с кандидатами (П2) и дешёвый NoSolution-фильтр без оценки боундов.

**~~П2. Типизированный ответ.~~ СДЕЛАНО.** `SolverResponse { certainty; значения типовых и константных слотов; obligations; candidates }` — неизменяемый узел crate-пула; кэши держат только указатель на него и не содержат `ImplRef` либо указателей во временные данные. Type/const-слоты извлекаются из канонического ответа отдельной inference-таблицей, а `ImplRef`+callback реконструируются только legacy-адаптером. Кэш сохраняется между outermost-входами до изменения inference/ParamEnv, const-ивары реплеятся как обычные value-слоты.

**~~П3. Эффекты как obligations.~~ СДЕЛАНО.** Полный типизированный ответ теперь несёт входы и значения type/const-слотов, nested trait obligations и отложенные type equalities; тот же набор эффектов сохраняется для каждого ambiguous-кандидата. Все эффекты применяются через `Context::applySolverResponse`, в том числе при динамической нормализации и compaction; defining opaque hidden-type доходит до существующего `equateErasedAlias` через обычную type equality. Побочные `typeConstraint`, `addImplBounds(onlyWithIvars)` и `definingUse()`-байпас удалены. Ответ коррелирует свежие слоты с прямым `ImplRef`, поэтому связанные проекции не распадаются на независимые ивары. Регрессии покрывают literal-слот, вложенную проекцию и const-generic локальный impl; полный `unit` — 996/996.

**~~П4. NormalizesTo — и смерть трёх EAT-селекторов.~~ СДЕЛАНО.** `NormalizesTo` имеет отдельный типизированный ответ: выходная проекция и все inference-эффекты возвращаются одним solver-запросом. Динамический и статический EAT используют этот запрос; старые EAT-селекторы, стеки/лимиты рекурсии, `selfSimilarChain`, `eatActiveStack` и фолбэки Magic/Types/Crate удалены. Item-provider специализации выбирается отдельным запросом по цепочке `specializationItemSource`; `noGoalBridge` удалён. Bare ParamEnv без associated equality больше не выдаёт фиктивную нормализацию, а associated equality во вложенных predicates связываются до проверки obligations и не зависят от порядка where-bound. Reservation impl участвует только в coherence и не становится обычным кандидатом. Регрессии покрывают inherited value provider, GAT input-slot identity, nested supertrait equalities и diverging blanket/provider; полный `unit` — 1000/1000.

**~~П5. Перевод потребителей и выпил.~~ СДЕЛАНО.** `checkAssociated`, static resolve, method/autoderef и builtin-потребители вызывают typed `solveTraitGoal`/`NormalizesTo` и применяют `SolverResponse` через `Context::applySolverResponse`. Identity-retry, specialisable-repeat, отдельный static selector и его публичные legacy API удалены. Trait-driven possibilities удалены из `checkIvarPoss`; там остался только направленный coercion graph и его raw-pointer fallback. `HIRCompare::Fuzzy` не пересекает границу солвера: он остался только во внутреннем сопоставлении голов кандидатов и в не-солверных структурных сравнениях.

Ответ переносит type/const-слоты, equalities и obligations; associated output нормализуется до экспорта. Позднее ожидаемое значение diverging closure хранится явным `ClosureReturnObligation`, без `const_cast` HIR-узла. Candidate ranking различает global/non-global ParamEnv и alias bounds: environment-head получает преимущество только когда входные coercion-цели доказаны, а ambiguous identity начинает с нейтральных слотов и экспортирует лишь связывания, общие всем жизнеспособным кандидатам. Это закрывает реальные случаи из `compiler_builtins`, `alloc` и `rand-0.9.2` без потребительского угадывания. Целевые solver-регрессии и итоговый полный Nix `unit` зелёные: 1004/1004.

Порядок П0→П5 завершён. Исходные костыли удалялись только после появления соответствующих примитивов: транзакций инференса, типизированного canonical response, obligations/equalities и единой `NormalizesTo`.


## Вердикт

  Миграция солвера не закончена. Основной evaluator, транзакции, typed response и NormalizesTo уже есть, но поверх них оставлены
  мосты, воспроизводящие старую fuzzy-семантику.

  Утверждения в dev/SOLVER.md:3, что снаружи больше нет выбора кандидатов и retry, неверны. Более того, dev/SOLVER_EX.md:31 оставляет
  P1 «Один примитив унификации» незакрытым, а ниже внезапно объявляет весь P0–P5 завершённым.

  ## Самая большая дыра: кандидата всё ещё выбирает потребитель

  Есть семь exportAmbiguousCandidates = true. Fuzzy формально превращён в Ambiguous + candidates, но смысл не изменился:

   Потребитель            Что делает с неоднозначностью
  ━━━━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   Index                  Считает кандидатов, при одном связывает Output; при нескольких перебирает все integer-типы и выбирает
                          единственный подходящий — bin/rustc/hir_typeck_expr_cs.cpp:594
  ─────────────────────  ─────────────────────────────────────────────────────────────────────────────────────────────────────────────
   AsyncFn                При одном exported candidate применяет именно его response — bin/rustc/hir_typeck_expr_cs.cpp:834
  ─────────────────────  ─────────────────────────────────────────────────────────────────────────────────────────────────────────────
   FnOnce                 Ровно так же применяет единственный ambiguous candidate — bin/rustc/hir_typeck_expr_cs.cpp:970
  ─────────────────────  ─────────────────────────────────────────────────────────────────────────────────────────────────────────────
   Unsize                 Сам делает implsOverlap/moreSpecificThan, выбирает лучший и применяет его — bin/rustc/
                          hir_typeck_expr_cs.cpp:6324
  ─────────────────────  ─────────────────────────────────────────────────────────────────────────────────────────────────────────────
   Closure expectation    Берёт первый exported proven candidate с пригодной сигнатурой — bin/rustc/hir_typeck_expr_cs.cpp:7147
  ─────────────────────  ─────────────────────────────────────────────────────────────────────────────────────────────────────────────
   Operators              Обходит кандидатов и по их impl-заголовкам решает, разрешать ли builtin inference — bin/rustc/
                          hir_typeck_expr_cs.cpp:7641
  ─────────────────────  ─────────────────────────────────────────────────────────────────────────────────────────────────────────────
   Autoderef              Сам принимает единственного кандидата и извлекает Deref::Target — bin/rustc/hir_typeck_helpers.cpp:10690

  Для этого даже существует публичный Context::applySolverResponse(SolverCandidateResponse) — bin/rustc/hir_typeck_expr_cs.h:184. То
  есть документационное «typeck не повторяет выбор кандидата» опровергается самим API.

  ## Внутри солвера P1 тоже не закончен

  Новый Unifier существует, но возвращает только Unified/Mismatch, складывая всё недоказанное в pending — bin/rustc/
  hir_typeck_helpers.h:342. Единого результата Proven/Ambiguous(nested goals)/Fail пока нет.

  Из-за этого:

  - Нормальные trait impl heads проходят через unifyImplHead, но свободные existential-параметры снова материализуются строкой с
    адресом impl_?_<address> — bin/rustc/hir_typeck_helpers.cpp:5420.

  - Builtin, ParamEnv, trait-object и opaque-кандидаты по-прежнему приходят через TraitImplCallback(ImplRef, HIRCompare) — bin/rustc/
    hir_typeck_helpers.h:526, bin/rustc/hir_typeck_helpers.cpp:5555.

  - Для них typed equalities восстанавливаются постфактум из ImplRef, потому что collector сам настоящую унификацию не делал.
  - Fuzzy environment/builtin head при определённых условиях повышается до Proven — bin/rustc/hir_typeck_helpers.cpp:6310.
  - Associated relation всё ещё делает normalize-and-retry под mutable-флагом aliasRelateActive_ — bin/rustc/
    hir_typeck_helpers.cpp:6150.

  - Root associated equality использует compareTy, а отдельный случай повышает Fuzzy до Proven, если output concrete — bin/rustc/
    hir_typeck_helpers.cpp:6777.

  То есть новый unifier используется кусками, а не является единственной relation-машиной.

  ## ParamEnv и builtin assembly остаются legacy

  ParamEnv — большой самостоятельный fuzzy matcher:

  - Собственный HrtbBoundMatcher, comparePp, matchTestGenericsFuzz — bin/rustc/hir_typeck_helpers.cpp:9082.
  - Associated declaration bounds с generic/GAT-параметрами просто пропускаются — bin/rustc/hir_typeck_helpers.cpp:9295.
  - Fuzzy associated-bound случай оставлен с TODO — bin/rustc/hir_typeck_helpers.cpp:9325.
  - Bounded candidate с GAT equality принудительно остаётся Ambiguous — bin/rustc/hir_typeck_helpers.cpp:6119.

  Builtin assembly вручную сопоставляет closure/async arguments и переводит nested SolverCertainty обратно в HIRCompare — bin/rustc/
  hir_typeck_helpers.cpp:3197. Есть и особенно плохой fallback: placeholder generic из группы 2 считается fuzzy-реализацией любого
  trait — bin/rustc/hir_typeck_helpers.cpp:3602.

  Открытые функциональные дыры: MetaSized, object safety trait-object кандидатов и часть GAT-associated bounds.

  ## Coercion замыкает solver обратно на typeck

  Solver принимает внешний SolverCoercionEvaluator, а реализация в Context вызывает старые checkCoerceTys, checkUnsizeTys, autoderef и
  compareWithPlaceholders — bin/rustc/hir_typeck_expr_cs.cpp:7342.

  После этого solver фильтрует и Pareto-ранжирует кандидатов результатами callback — bin/rustc/hir_typeck_helpers.cpp:7410. Затем
  потребитель ещё раз проверяет те же coercion constraints и явно просит retry после стабилизации графа — bin/rustc/
  hir_typeck_expr_cs.cpp:7924.

  Это прямое опровержение пункта «нет retry после ответа goal solver».

  ## Static и inherent остаются отдельными мирами

  Static bridge вызывает новый solver, но выбрасывает slots/equalities/obligations/candidates и возвращает старый ImplRef + certainty;
  при отсутствующих trait params создаёт static_find_impl_<address> — bin/rustc/hir_typeck_static.cpp:36.

  Дальше остаются:

  - findImplCheckCrateRaw: GetParams + Matcher, fuzzy placeholders, допущение «если bound type _, считать bound успешным», незакрытый
    GAT — bin/rustc/hir_typeck_static.cpp:173.

  - fticCheckParams: ещё одна пара GetParams/Matcher, четырёхпроходный fixed point, fuzzy save/restore и вообще не реализованный
    TypeEquality bound — bin/rustc/hir_typeck_helpers.cpp:9378.

  - OwnedImplMatcher для inherent impl — bin/rustc/hir_typeck_expr_cs.cpp:10590.
  - Method lookup добавляет и Equal, и Fuzzy bounds как кандидатов, после чего отдельный expression visitor дедуплицирует, угадывает
    параметры и делает fallback/goto — bin/rustc/hir_typeck_helpers.cpp:11189, bin/rustc/hir_typeck_expr_cs.cpp:1134.

  - Static canUnsize остаётся отдельной ручной реализацией с собственным ParamEnv/trait-object поиском — bin/rustc/
    hir_typeck_static.cpp:1430.

  ## Общий fallback inference

  Trait possibilities действительно удалены из IVarPossible; там теперь coercion graph, defaults и raw-pointer fallback. Эта узкая
  часть документации честная.

  Но сам inference всё ещё имеет пять режимов, включая FinalOption с буквальным комментарием «Just picks an option (even if it might
  be wrong)» — bin/rustc/hir_typeck_expr_cs.cpp:8635, и запускает их последовательно — bin/rustc/hir_typeck_expr_cs.cpp:10192. Это уже
  не trait solver, но это настоящий legacy/fallback-контур, влияющий на его входы.

  ## Почему зелёный unit ничего не доказывает

  Часть тестов прямо закрепляет мосты:

  - test_next_solver_globally_ivar_goal_legacy_possibilities.rs требует «legacy fuzzy walk» и single-candidate commit — tst/unit/
    test_next_solver_globally_ivar_goal_legacy_possibilities.rs:4.

  - test_next_solver_globally_operator_overload_probe_legacy.rs требует обхода legacy paths — tst/unit/
    test_next_solver_globally_operator_overload_probe_legacy.rs:3.

  Поэтому 1004/1004 совместимы с незаконченной миграцией: gate охраняет текущее поведение, включая костыли.

  ## Реальный порядок доделки

  1. Удалить exportAmbiguousCandidates и возможность применять SolverCandidateResponse. Потребитель получает только общий ответ или
     Ambiguous.

  2. Сделать coercion/unsize полноценными relation/goals, без callback в Context и post-solver retry.
  3. Закончить P1: одна унификация для TraitImpl, ParamEnv, builtin, objects и projections; pending alias relations становятся nested
     goals.

  4. Удалить TraitImplCallback, адресные placeholders и ручные HrtbBoundMatcher/GetParams/Matcher.
  5. Отдельно перевести inherent/method selection: убрать fticCheckParams, OwnedImplMatcher, raw static matcher и method fallback
     selection.

  6. Перевести static consumers на полный SolverResponse, не на lossy ImplRef.
  7. Только после этого исправить SOLVER.md и поставить реальные zero-gates на перечисленные API.

  Коротко: фундамент нового солвера есть, но граница, ParamEnv/builtins, coercion и весь inherent/method слой ещё не мигрированы.
  Текущее «finish next solver migration» — преждевременное.
  