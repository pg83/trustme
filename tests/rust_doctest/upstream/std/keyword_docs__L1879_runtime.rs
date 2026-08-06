// Extracted from library/std/src/keyword_docs.rs:1879
#![allow(unused)]
fn main() {
    use std::sync::{Arc, Mutex};
    type ArcMutex<T> = Arc<Mutex<T>>;
}
