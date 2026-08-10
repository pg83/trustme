// Extracted from src/generics/assoc_items/types.md:8
#![allow(unused)]
fn main() {
    // `A` and `B` are defined in the trait via the `type` keyword.
    // (Note: `type` in this context is different from `type` when used for
    // aliases).
    trait Contains {
        type A;
        type B;
    
        // Updated syntax to refer to these new types generically.
        fn contains(&self, _: &Self::A, _: &Self::B) -> bool;
    }
}
