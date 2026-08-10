// Extracted from library/alloc/src/vec/mod.rs:2953
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = vec![1, 2, 3];
    vec.resize_with(5, Default::default);
    assert_eq!(vec, [1, 2, 3, 0, 0]);

    let mut vec = vec![];
    let mut p = 1;
    vec.resize_with(4, || { p *= 2; p });
    assert_eq!(vec, [2, 4, 8, 16]);
}
