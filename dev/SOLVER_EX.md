Нет. Отдельный legacy solver удалён, но его fuzzy-семантика выкорчевана не полностью.

  Аудит сделан на 21e58718016a, tracked worktree чистый.

  ### Что действительно удалено

  - ImplRef, fromLegacy(), .legacy(), BindPlaceholders, BindResponse, SolverCandidateResponse, OwnedImplMatcher,
    SolverCoercionEvaluator и файлы hir_typeck_impl_ref.* отсутствуют.

  - Все trait goals входят через один bin/rustc/hir_typeck_helpers.cpp:3757 → NextTraitGoalEvaluator.
  - Обычные impl-кандидаты после fuzzy-префильтра обязательно проходят transactional bin/rustc/hir_typeck_helpers.cpp:9263.
  - При нескольких разных кандидатах solver больше не экспортирует отдельные heads: он пересекает общие slots/equalities/obligations.
  - StaticTraitResolve::findImpl — уже не второй solver, а bridge в новый.

  ### Что реально осталось

  1. Старый fuzzy Unsize жив целиком.

  bin/rustc/hir_typeck_helpers.cpp:5145 — большой рекурсивный решатель на HIRCompare. Он сам решает:

  - struct-tail unsizing;
  - array → slice;
  - trait-object upcast;
  - supertraits и markers;
  - associated types;
  - ParamEnv bounds.

  Новый bin/rustc/hir_typeck_helpers.cpp:5474 вызывает его напрямую на bin/rustc/hir_typeck_helpers.cpp:5545 и просто переводит Equal/
  Fuzzy/Unequal в Proven/Ambiguous/NoSolution.

  Builtin Unsize assembly тоже идёт через этот движок: bin/rustc/hir_typeck_helpers.cpp:3037.

  Это именно legacy solver под новым enum, не prefilter.

  2. Coercion/unsize реализован параллельно второй раз.

  В expression typeck остаются:

  - bin/rustc/hir_typeck_expr_cs.cpp:892;
  - bin/rustc/hir_typeck_expr_cs.cpp:1486;
  - отдельный fuzzy matcher trait-object projections и markers начиная с bin/rustc/hir_typeck_expr_cs.cpp:1077;
  - ручное сравнение struct fields на bin/rustc/hir_typeck_expr_cs.cpp:1613.

  Таким образом, применимость coercion решают как минимум два независимых алгоритма: solver ranking и expression typeck/rewrite.

  3. Builtin Sized, Copy, Clone всё ещё доказуются старым tri-state кодом.

  - bin/rustc/hir_typeck_helpers.cpp:4853
  - bin/rustc/hir_typeck_helpers.cpp:4986
  - bin/rustc/hir_typeck_helpers.cpp:5059

  Их HIRCompare напрямую превращается в certainty при candidate assembly на bin/rustc/hir_typeck_helpers.cpp:2844. Copy/Clone для
  generic также используют старый iterateBoundsTraits; callback местами вообще игнорирует полученный cmp.

  Это semantic proof, не структурный фильтр.

  4. Ambiguous + impl всё ещё является скрытым fuzzy candidate.

  Solver намеренно экспортирует выбранный impl даже при Ambiguous: bin/rustc/hir_typeck_helpers.cpp:12075. Причём
  SolverAmbiguityPolicy::Suppress подавляет только искусственный identity-ответ, а не такой ambiguous impl.

  Его используют как выбранный:

  - bin/rustc/hir_typeck_helpers.cpp:5816: ambiguous impl превращается в AutoderefResult::Match;
  - bin/rustc/hir_typeck_expr_cs.cpp:8470: любой response.impl применяется и считается selected;
  - bin/rustc/hir_typeck_static.cpp:1842: при конкретных входах ambiguous impl выбирает тело метода/константу;
  - bin/rustc/hir_typeck_helpers.cpp:6773: response применяется и метод считается найденным; ambiguity обязательно останавливает выбор
    только при open receiver;

  - bin/rustc/hir_typeck_expr_cs.cpp:2627: применяет ambiguous response и часто возвращает Complete;
  - UFCS lowering принимает всё, кроме NoSolution: bin/rustc/hir_conv_main_bindings.cpp:3723;
  - codegen-пути местами проверяют только response.impl: bin/rustc/trans_main_bindings.cpp:793, bin/rustc/
    trans_main_bindings.cpp:1247.

  То есть API всё ещё смешивает два разных понятия: «может примениться» и «доказан и может быть выбран».

  5. Method lookup остаётся отдельным selector-контуром.

  bin/rustc/hir_typeck_helpers.cpp:6238 вручную:

  - перечисляет inherent, ParamEnv, trait-object, associated и in-scope методы;
  - проверяет аргументы через отдельный coercion engine;
  - канонизирует и схлопывает candidates;
  - отдельно ранжирует current trait и supertraits.

  Custom receiver по-прежнему извлекается через HIRMatchGenerics с TODO: bin/rustc/hir_typeck_helpers.cpp:6186.

  Это не второй полноценный trait solver, но существенная параллельная машина выбора осталась.

  6. Старый inference fallback остаётся.

  checkIvarPoss имеет пять последовательных режимов:

  None → Backwards → Assume → IgnoreWeakDisable → FinalOption

  Определение: bin/rustc/hir_typeck_expr_cs.cpp:2953, запуски: bin/rustc/hir_typeck_expr_cs.cpp:7128.

  Trait-driven enumeration оттуда удалён, поэтому это уже не legacy trait solver. Но это всё ещё эвристический coercion/inference
  fallback, который формирует входные ограничения solver-а.

  ### Что можно оставить как structural fuzzy

  Нормальный допустимый случай — HIR-index:

  bin/rustc/hir_hir.cpp:202 использует matchTestGenericsFuzz, после чего каждый найденный impl проверяется настоящим Unifier на bin/
  rustc/hir_typeck_helpers.cpp:9486.

  Также сами общие structural comparisons в hir_type.cpp/hir_path.cpp и typeIsInteriorMutable не являются trait solver-ом.

  unifyCandidateParams/bindCandidateResponse — не просто переименованный fuzzy matcher: они используют настоящий Unifier под snapshot.
  Функционально старый binding-этап сохранился, но старого fuzzy-примитива там уже нет.

  ### Гейты и мусор

  Текущий C++ gate проверяет только форму структур и overload applySolverResponse: bin/rustc/hir_typeck_helpers_ut.cpp:10. Все
  перечисленные fuzzy-решения он пропускает.

  Документация противоречит коду:

  - dev/SOLVER.md:3 заявляет, что consumer больше не выбирает fuzzy candidate — неверно.
  - Начало dev/SOLVER_EX.md:1 говорит правду, а финал на dev/SOLVER_EX.md:320 снова объявляет всё закрытым.
  - -Znext-solver* остался no-op.
  - NotYetKnown и ложные *legacy* названия/комментарии тестов остались.
  - В bin/rustc/hir_typeck_expr_cs.cpp:2624 есть буквально пустой if.

  ### Итог

  Отдельный legacy solver удалён архитектурно. Но задача «убрать legacy/fuzzy поведение» не завершена.

  Порядок добивания:

  1. Сделать единственную typed coercion/unsize relation и удалить canUnsizeCb плюс решающую часть checkUnsizeTys/checkCoerceTys.
  2. Запретить impl в ambiguous-ответе; выделить отдельный явный MayApply probe.
  3. Перевести Sized/Copy/Clone на SolverCertainty и nested goals без HIRCompare.
  4. Исправить autoderef, index, static value, method, UFCS и codegen consumers.
  5. После этого сворачивать ручной method selector и старый bound iterator.
  6. Добавить gate, запрещающий semantic HIRCompare/Fuzzy внутри solver/coercion путей, с узким исключением только для HIR-index
     prefilter.

─ Worked for 9m 19s ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────

 
› Summarize recent commits
 
  gpt-5.6-sol xhigh fast · ~/monorepo/trustme