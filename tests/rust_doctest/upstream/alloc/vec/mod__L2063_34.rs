// Extracted from library/alloc/src/vec/mod.rs:2063
#![allow(unused)]
#![feature(push_mut)]
extern crate alloc;
fn main() {
    let mut vec = vec![1, 3, 5, 9];
    let x = vec.insert_mut(3, 6);
    *x += 1;
    assert_eq!(vec, [1, 3, 5, 7, 9]);
}
