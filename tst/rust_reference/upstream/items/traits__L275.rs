// Extracted from src/items/traits.md:275
#![allow(unused)]
fn main() {
    trait T {
        fn f1(&self);
        fn f2(x: Self, _: i32);
    }
}
