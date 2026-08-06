// Extracted from library/alloc/src/vec/mod.rs:1293
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec: Vec<i32> = Vec::with_capacity(10);
    vec.push(42);
    assert!(vec.capacity() >= 10);
}
