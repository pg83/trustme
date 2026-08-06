// Extracted from library/alloc/src/sync.rs:161
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    let foo = Arc::new(vec![1.0, 2.0, 3.0]);
    // The two syntaxes below are equivalent.
    let a = foo.clone();
    let b = Arc::clone(&foo);
    // a, b, and foo are all Arcs that point to the same memory location
}
