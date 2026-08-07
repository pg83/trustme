// Extracted from library/core/src/iter/traits/iterator.rs:2156
#![allow(unused)]
#![feature(iter_collect_into)]
fn main() {

    let a = [1, 2, 3];
    let mut vec: Vec::<i32> = Vec::with_capacity(6);

    let count = a.iter().collect_into(&mut vec).iter().count();

    assert_eq!(count, vec.len());
    assert_eq!(vec, vec![1, 2, 3]);

    let count = a.iter().collect_into(&mut vec).iter().count();

    assert_eq!(count, vec.len());
    assert_eq!(vec, vec![1, 2, 3, 1, 2, 3]);
}
