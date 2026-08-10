// Extracted from src/fn/closures/closure_examples/iter_any.md:8
#![allow(unused)]
fn main() {
    pub trait Iterator {
        // The type being iterated over.
        type Item;
    
        // `any` takes `&mut self` meaning the caller may be borrowed
        // and modified, but not consumed.
        fn any<F>(&mut self, f: F) -> bool where
            // `FnMut` meaning any captured variable may at most be
            // modified, not consumed. `Self::Item` is the closure parameter type,
            // which is determined by the iterator (e.g., `&T` for `.iter()`,
            // `T` for `.into_iter()`).
            F: FnMut(Self::Item) -> bool;
    }
}
