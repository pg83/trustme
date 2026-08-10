// Extracted from library/alloc/src/vec/mod.rs:1493
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = Vec::with_capacity(10);
    vec.extend([1, 2, 3]);
    assert!(vec.capacity() >= 10);
    vec.shrink_to(4);
    assert!(vec.capacity() >= 4);
    vec.shrink_to(0);
    assert!(vec.capacity() >= 3);
}
