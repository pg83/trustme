// Extracted from library/core/src/iter/traits/iterator.rs:2084
#![allow(unused)]
#![feature(iterator_try_collect)]
fn main() {

    use core::ops::ControlFlow::{Break, Continue};

    let u = [Continue(1), Continue(2), Break(3), Continue(4), Continue(5)];
    let mut it = u.into_iter();

    let v = it.try_collect::<Vec<_>>();
    assert_eq!(v, Break(3));

    let v = it.try_collect::<Vec<_>>();
    assert_eq!(v, Continue(vec![4, 5]));
}
