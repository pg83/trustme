// Extracted from src/items/associated-items.md:343
#![allow(unused)]
fn main() {
    use std::fmt::Debug;
    trait Example {
        type Output<T>: Ord where T: Debug;
    }
}
