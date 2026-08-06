// Extracted from src/fn/closures/anonymity.md:9
#![allow(unused)]
fn main() {
    // `F` must be generic.
    fn apply<F>(f: F) where
        F: FnOnce() {
        f();
    }
}
