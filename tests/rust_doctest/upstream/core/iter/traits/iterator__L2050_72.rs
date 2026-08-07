// Extracted from library/core/src/iter/traits/iterator.rs:2050
#![allow(unused)]
#![feature(iterator_try_collect)]
fn main() {

    let u = vec![Some(1), Some(2), Some(3)];
    let v = u.into_iter().try_collect::<Vec<i32>>();
    assert_eq!(v, Some(vec![1, 2, 3]));
}
