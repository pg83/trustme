// Extracted from library/core/src/iter/traits/iterator.rs:2255
#![allow(unused)]
#![feature(iter_partition_in_place)]
fn main() {

    let mut a = [1, 2, 3, 4, 5, 6, 7];

    // Partition in-place between evens and odds
    let i = a.iter_mut().partition_in_place(|n| n % 2 == 0);

    assert_eq!(i, 3);
    assert!(a[..i].iter().all(|n| n % 2 == 0)); // evens
    assert!(a[i..].iter().all(|n| n % 2 == 1)); // odds
}
