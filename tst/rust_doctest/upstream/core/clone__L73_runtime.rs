// Extracted from library/core/src/clone.rs:73
#![allow(unused)]
fn main() {
    // `derive` implements Clone for Reading<T> when T is Clone.
    #[derive(Clone)]
    struct Reading<T> {
        frequency: T,
    }
}
