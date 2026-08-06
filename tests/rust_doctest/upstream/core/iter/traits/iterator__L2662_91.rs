// Extracted from library/core/src/iter/traits/iterator.rs:2662
#![allow(unused)]
#![feature(iterator_try_reduce)]
fn main() {
    
    let numbers: Vec<usize> = vec![10, 20, 5, 23, 0];
    let sum = numbers.into_iter().try_reduce(|x, y| x.checked_add(y));
    assert_eq!(sum, Some(Some(58)));
}
