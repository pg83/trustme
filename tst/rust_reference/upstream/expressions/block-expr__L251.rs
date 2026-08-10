// Extracted from src/expressions/block-expr.md:251
#![allow(unused)]
fn main() {
    fn foo<T>() -> usize {
        const { std::mem::size_of::<T>() + 1 }
    }
}
