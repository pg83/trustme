// Extracted from library/core/src/iter/traits/iterator.rs:2672
#![allow(unused)]
#![feature(iterator_try_reduce)]
fn main() {

    let numbers = vec![1, 2, 3, usize::MAX, 4, 5];
    let sum = numbers.into_iter().try_reduce(|x, y| x.checked_add(y));
    assert_eq!(sum, None);
}
