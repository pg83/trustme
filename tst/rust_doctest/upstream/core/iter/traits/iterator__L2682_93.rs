// Extracted from library/core/src/iter/traits/iterator.rs:2682
#![allow(unused)]
#![feature(iterator_try_reduce)]
fn main() {

    let numbers: Vec<usize> = Vec::new();
    let sum = numbers.into_iter().try_reduce(|x, y| x.checked_add(y));
    assert_eq!(sum, Some(None));
}
