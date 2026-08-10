// Extracted from src/items/traits.md:282
#![allow(unused)]
fn main() {
    trait T {
        fn f2(&x: &i32); // ERROR: patterns aren't allowed in functions without bodies
    }
}
