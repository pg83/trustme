// Extracted from library/alloc/src/vec/mod.rs:2687
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = vec![1, 2, 3, 4];
    let pred = |x: &mut i32| *x % 2 == 0;

    assert_eq!(vec.pop_if(pred), Some(4));
    assert_eq!(vec, [1, 2, 3]);
    assert_eq!(vec.pop_if(pred), None);
}
