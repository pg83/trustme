// Extracted from src/items/associated-items.md:361
#![allow(unused)]
fn main() {
    trait LendingIterator {
        type Item<'x> where Self: 'x;
        fn next<'a>(&'a mut self) -> Self::Item<'a>;
    }
}
