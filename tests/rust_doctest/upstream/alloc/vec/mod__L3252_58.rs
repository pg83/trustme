// Extracted from library/alloc/src/vec/mod.rs:3252
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = vec![1];
    vec.extend_from_slice(&[2, 3, 4]);
    assert_eq!(vec, [1, 2, 3, 4]);
}
