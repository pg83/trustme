// Extracted from library/alloc/src/vec/mod.rs:2575
#![allow(unused)]
#![feature(push_mut)]
extern crate alloc;
fn main() {


    let mut vec = vec![1, 2];
    let last = vec.push_mut(3);
    assert_eq!(*last, 3);
    assert_eq!(vec, [1, 2, 3]);

    let last = vec.push_mut(3);
    *last += 1;
    assert_eq!(vec, [1, 2, 3, 4]);
}
