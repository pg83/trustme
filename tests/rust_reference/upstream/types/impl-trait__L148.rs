// Extracted from src/types/impl-trait.md:148
#![allow(unused)]
fn main() {
    trait Trait {}
    fn foo<T: Trait>() -> T {
        // ...
    panic!()
    }
}
