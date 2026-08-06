// Extracted from src/items/associated-items.md:386
#![allow(unused)]
fn main() {
    trait Iterable {
        type Item<'a> where Self: 'a;
        type Iterator<'a>: Iterator<Item = Self::Item<'a>> where Self: 'a;
        fn iter<'a>(&'a self) -> Self::Iterator<'a>;
    }
}
