// Extracted from src/items/associated-items.md:373
#![allow(unused)]
fn main() {
    trait Check<T> {
        type Checker<'x>;
        fn create_checker<'a>(item: &'a T) -> Self::Checker<'a>;
        fn do_check(checker: Self::Checker<'_>);
    }
}
