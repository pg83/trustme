// Extracted from src/types/impl-trait.md:160
#![allow(unused)]
fn main() {
    trait Trait {}
    impl Trait for () {}
    fn foo() -> impl Trait {
        // ...
    }
}
