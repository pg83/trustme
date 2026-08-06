// Extracted from library/alloc/src/vec/mod.rs:2657
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = vec![1, 2, 3];
    assert_eq!(vec.pop(), Some(3));
    assert_eq!(vec, [1, 2]);
}
