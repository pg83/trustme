// Extracted from library/alloc/src/vec/mod.rs:2193
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = vec![1, 2, 3, 4, 5];
    let keep = [false, true, true, false, true];
    let mut iter = keep.iter();
    vec.retain(|_| *iter.next().unwrap());
    assert_eq!(vec, [2, 3, 5]);
}
