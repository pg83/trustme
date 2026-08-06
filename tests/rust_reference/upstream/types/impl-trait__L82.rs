// Extracted from src/types/impl-trait.md:82
#![allow(unused)]
fn main() {
    fn returns_closure() -> impl Fn(i32) -> i32 {
        |x| x + 1
    }
}
