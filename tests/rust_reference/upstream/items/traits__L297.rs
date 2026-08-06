// Extracted from src/items/traits.md:297
#![allow(unused)]
fn main() {
    trait T {
        fn f1(123: i32) {} // ERROR: pattern is refutable
        fn f2(Some(x): Option<i32>) {} // ERROR: pattern is refutable
    }
}
