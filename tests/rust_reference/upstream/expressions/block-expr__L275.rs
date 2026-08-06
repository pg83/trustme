// Extracted from src/expressions/block-expr.md:275
#![allow(unused)]
fn main() {
    fn foo<T>() -> usize {
        // If this code ever gets executed, then the assertion has definitely
        // been evaluated at compile-time.
        const { assert!(std::mem::size_of::<T>() > 0); }
        // Here we can have unsafe code relying on the type being non-zero-sized.
        /* ... */
        42
    }
}
