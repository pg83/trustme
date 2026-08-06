// Extracted from src/items/associated-items.md:399
#![allow(unused)]
fn main() {
    trait StaticReturn {
        type Y<'a>;
        fn foo(&self) -> Self::Y<'static>;
    }
}
