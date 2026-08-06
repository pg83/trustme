Понял задачу: нам нужны готовые Rust-программы с наблюдаемым результатом — `assert!`, код возврата или ожидаемый stdout. Компилируем trustme, запускаем полученный бинарь, тем самым проверяем семантику сгенерированного кода. Родной формат тестового набора не важен.

## Где брать

### 1. Rust 1.90: `tests/ui` с `run-pass` — основной источник

В точном теге [Rust 1.90.0](https://github.com/rust-lang/rust/tree/1.90.0/tests/ui):

- 19 032 `.rs`-файла всего;
- 3 698 тестов помечены `run-pass`/`run-pass-valgrind`;
- 1 939 из них содержат `assert!`, `assert_eq!` или `assert_ne!`;
- даже грубый фильтр «есть `main`, есть assert, ≤200 строк, без aux-crates и feature gates» даёт 1 197 автономных кандидатов.

Это именно нужная модель: compiletest компилирует программу, запускает её и считает панику/ненулевой exit провалом. [Документация compiletest](https://rustc-dev-guide.rust-lang.org/tests/compiletest.html), [семантика `run-pass`](https://rustc-dev-guide.rust-lang.org/tests/ui.html).

Примеры совсем атомарные:

- [i32-sub.rs](https://github.com/rust-lang/rust/blob/1.90.0/tests/ui/numbers-arithmetic/i32-sub.rs) — 6 строк;
- [operator-associativity.rs](https://github.com/rust-lang/rust/blob/1.90.0/tests/ui/parser/operator-associativity.rs) — 4 строки;
- `generic-tup.rs`, `for-destruct.rs`, `fixed_length_copy.rs`, тесты match, closures, consts, enum layout.

Это первый источник, который стоит импортировать целиком, а не выбирать руками сотню файлов.

### 2. Тесты `core`/`alloc`/`std` из того же архива

В уже используемом нами Rust 1.90 я насчитал:

| Набор | `#[test]` | Вхождений assert-макросов |
|---|---:|---:|
| `coretests` | 1 303 | 6 772 |
| `alloctests` | 759 | 2 339 |
| `std/tests` | 440 | 987 |
| Всего | 2 502 | 10 098 |

Исходники: [coretests](https://github.com/rust-lang/rust/tree/1.90.0/library/coretests/tests), [alloctests](https://github.com/rust-lang/rust/tree/1.90.0/library/alloctests/tests), [std/tests](https://github.com/rust-lang/rust/tree/1.90.0/library/std/tests).

Там очень плотные инварианты:

```rust
assert_eq!(a * (a - 1), 90);
assert_eq!(i32_a << 16, 655360);
assert_eq!(x.and(Some(2)), Some(2));
assert_eq!(*drop_counter.borrow(), 1);
```

Они проверяют одновременно:

- арифметику и casts;
- enum/niche/layout;
- drop и move;
- ссылки и указатели;
- итераторы и closures;
- строки, slices, arrays;
- generics, traits и dispatch.

Их не требуется превращать в самостоятельные файлы вручную. Адаптер может либо вызвать наш уже поддерживаемый `--test`/libtest, либо генерировать по исполняемому wrapper на тестовый модуль.

Особенно удобно, что полный исходник 1.90 у нас уже является артефактом `std_src`: [build.py](/home/pg/monorepo/trustme/build.py:227), [fetch.py](/home/pg/monorepo/trustme/tests/std/fetch.py:18). Повторно вендорить его нет смысла.

### 3. Doctests стандартной библиотеки

В документации `core`, `alloc` и `std` тега 1.90:

- около 4 972 fenced Rust-блоков;
- 3 165 содержат assert-макросы;
- примерно 3 055 выглядят исполняемыми, без очевидных `no_run`, `ignore` или `compile_fail`.

Rustdoc штатно добавляет отсутствующий `fn main`, обрабатывает скрытые строки и запускает пример; это хорошо документированный формат. [Правила doctest](https://doc.rust-lang.org/rustdoc/write-documentation/documentation-tests.html).

Нужен небольшой extractor-adapter, повторяющий эти правила. Это фактически ещё несколько тысяч маленьких программ вроде `assert_eq!(slice.reverse(), ...)`, причём уже привязанных к нашему точному libstd.

### 4. gccrs `execute/torture` — лучший независимый микрокорпус

В [gccrs execute/torture](https://github.com/Rust-GCC/gccrs/tree/master/gcc/testsuite/rust/execute/torture):

- 302 исполняемых `.rs`;
- 262 лежат именно в `torture`;
- 301 имеют `main`;
- тесты обычно занимают 5–20 строк.

Пример:

```rust
#![feature(no_core)]
#![no_core]

fn main() -> i32 {
    [55, 66, 77][1] - 66
}
```

Нулевой результат — успех. Есть tuple destructuring, блоковые выражения, арифметика, arrays, const evaluation, match, macros, generics и intrinsics. Часть тестов задаёт ожидаемый stdout через `dg-output`.

Для них нужен собственный адаптер:

- распознать несколько `dg-*` директив;
- собрать `no_core`;
- запустить;
- проверить exit/stdout.

Переписывать их в `assert_eq!` не надо. Ненулевой exit — такой же runtime-инвариант, только без зависимости от `std`.

### 5. Rust Quiz — 37 коротких, но злых семантических тестов

В [dtolnay/rust-quiz](https://github.com/dtolnay/rust-quiz/tree/master/questions) сейчас 37 самостоятельных программ с точным ожидаемым stdout.

Они специально фиксируют неочевидную семантику:

- statement boundaries;
- method lookup/autoderef;
- macro matching и hygiene;
- drop order;
- temporary lifetime extension;
- closure capture и `FnMut`;
- ranges, patterns, type inference.

Adapter читает `Answer:` из соседнего `.md`, запускает `.rs` и сравнивает stdout. Маленький набор, но потенциально очень ценный по числу проверяемых углов на строку.

### 6. Rustlings 6.5 — простейший базовый слой

[Rustlings 6.5.0](https://github.com/rust-lang/rustlings/releases/tag/6.5.0) требует минимум Rust 1.88 и поэтому подходит к нашей 1.90. В репозитории есть [официальные solutions](https://github.com/rust-lang/rustlings/tree/main/solutions) и тесты упражнений.

Покрытие элементарное, зато систематическое:

- variables/functions/if;
- primitives и vectors;
- moves;
- structs/enums;
- options/results;
- generics/traits/lifetimes;
- iterators, smart pointers, threads, macros.

Это хороший smoke-слой, но после официального run-pass: он заметно меньше и менее изощрён.

### 7. RustSmith — не склад, а генератор бесконечного склада

[RustSmith](https://github.com/rustsmith/rustsmith) генерирует корректные завершающиеся программы с определённым результатом и специально предназначен для дифференциального тестирования компиляторов. Авторы уже нашли им неизвестные ошибки именно в mrustc. [Статья и результаты](https://www.doc.ic.ac.uk/~afd/papers/2023/ISSTA-tool.pdf).

Практическая схема:

1. Генерируем программы по фиксированным seed.
2. Собираем и запускаем официальным rustc 1.90.
3. Сохраняем stdout как oracle.
4. В CI компилируем те же исходники trustme и сравниваем результат.
5. Интересные падения минимизируем и превращаем в обычные `tests/unit/test_*.rs`.

По умолчанию RustSmith генерирует примерно 3 000 строк, поэтому это не замена маленьким unit-тестам, а следующий слой после фиксированных корпусов.

Miri тоже годится, но уже точечно: в [Miri `tests/pass`](https://github.com/rust-lang/miri/tree/master/tests/pass) 375 файлов, 242 с assert-макросами. Они особенно полезны для pointers/drop/layout/unsafe, однако master привязан к текущему nightly; надо брать commit, соответствующий Rust 1.90. Отрицательные UB-тесты нативно запускать нельзя — только положительные `pass`.

## Как встроить по образцу `shitty`

Схема должна быть такой же:

- upstream сохраняется без ручной переписи;
- для каждого семейства свой adapter;
- catalog перечисляет все обнаруженные случаи;
- validator запрещает необъяснимые пропуски;
- XFAIL известен явно, неожиданный XPASS ломает тест;
- каждый отчёт содержит исходный upstream path;
- большие наборы шардируются, но логически каждый `.rs` остаётся отдельным тестом.

Именно так `shitty` заводит corpus-specific adapters и отдельные build nodes: [build.py](/home/pg/monorepo/shitty/build.py:1034). Для больших корпусов там же используется фиксированное шардирование.

## Мой порядок внедрения

1. Универсальный `compile_and_run` adapter.
2. Все подходящие `run-pass` из Rust 1.90.
3. gccrs `execute/torture`.
4. Rust Quiz.
5. Adapter для `#[test]`: `coretests` → `alloctests` → `std/tests`.
6. Doctest extractor.
7. Фиксированный RustSmith corpus.
8. Положительные Miri tests.

Ferrocene 25.11 можно использовать как индекс приоритета: этот выпуск соответствует Rust 1.90, а большинство их compiler tests размечено ссылками на разделы спецификации. Это позволяет выбирать именно нормативную семантику, а не rustc-внутренности. [Методика и traceability](https://public-docs.ferrocene.dev/main/qualification/evaluation-report/rustc/method.html), [Ferrocene 25.11 / Rust 1.90](https://public-docs.ferrocene.dev/main/release-notes/25.11.0.html).

Итого: у нас прямо сейчас доступно порядка 3 700 официальных исполняемых compiler tests, 2 500 библиотечных test-функций, около 3 000 assert-bearing doctests и ещё 339 особенно компактных независимых программ из gccrs и Rust Quiz. Это уже нормальная база без сочинения собственных `assert(1 + 1 == 2)`.
