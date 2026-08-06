// Extracted from src/items/associated-items.md:316
#![allow(unused)]
fn main() {
    trait Container {
        type E;
        fn empty() -> Self;
        fn insert(&mut self, elem: Self::E);
    }
}
