// Extracted from src/items/associated-items.md:146
#![allow(unused)]
fn main() {
    trait Changer: Sized {
        fn change(mut self) {}
        fn modify(mut self: Box<Self>) {}
    }
}
