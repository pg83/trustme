// Extracted from library/alloc/src/vec/mod.rs:1464
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = Vec::with_capacity(10);
    vec.extend([1, 2, 3]);
    assert!(vec.capacity() >= 10);
    vec.shrink_to_fit();
    assert!(vec.capacity() >= 3);
}
