// Extracted from src/fn/closures/closure_examples/iter_find.md:8
#![allow(unused)]
fn main() {
    pub trait Iterator {
        // The type being iterated over.
        type Item;
    
        // `find` takes `&mut self` meaning the caller may be borrowed
        // and modified, but not consumed.
        fn find<P>(&mut self, predicate: P) -> Option<Self::Item> where
            // `FnMut` meaning any captured variable may at most be
            // modified, not consumed. `&Self::Item` states it takes
            // arguments to the closure by reference.
            P: FnMut(&Self::Item) -> bool;
    }
}
