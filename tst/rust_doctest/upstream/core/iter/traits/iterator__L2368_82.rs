// Extracted from library/core/src/iter/traits/iterator.rs:2368
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];

    // the checked sum of all of the elements of the array
    let sum = a.into_iter().try_fold(0i8, |acc, x| acc.checked_add(x));

    assert_eq!(sum, Some(6));
}
