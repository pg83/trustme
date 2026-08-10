// Extracted from src/items/traits.md:291
#![allow(unused)]
fn main() {
    trait T {
        fn f1((a, b): (i32, i32)) {} // OK: is irrefutable
    }
}
