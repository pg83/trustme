// Extracted from library/alloc/src/vec/mod.rs:1362
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = vec![1];
    vec.reserve_exact(10);
    assert!(vec.capacity() >= 11);
}
