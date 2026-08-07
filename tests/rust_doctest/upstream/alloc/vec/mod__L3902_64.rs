// Extracted from library/alloc/src/vec/mod.rs:3902
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut numbers = vec![1, 2, 3, 4, 5, 6, 8, 9, 11, 13, 14, 15];

    let evens = numbers.extract_if(.., |x| *x % 2 == 0).collect::<Vec<_>>();
    let odds = numbers;

    assert_eq!(evens, vec![2, 4, 6, 8, 14]);
    assert_eq!(odds, vec![1, 3, 5, 9, 11, 13, 15]);
}
