// Extracted from library/alloc/src/vec/mod.rs:1330
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = vec![1];
    vec.reserve(10);
    assert!(vec.capacity() >= 11);
}
