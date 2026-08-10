// Extracted from library/std/src/collections/hash/set.rs:296
#![allow(unused)]
fn main() {
    use std::collections::HashSet;

    let mut set: HashSet<i32> = (0..8).collect();
    let extracted: HashSet<i32> = set.extract_if(|v| v % 2 == 0).collect();

    let mut evens = extracted.into_iter().collect::<Vec<_>>();
    let mut odds = set.into_iter().collect::<Vec<_>>();
    evens.sort();
    odds.sort();

    assert_eq!(evens, vec![0, 2, 4, 6]);
    assert_eq!(odds, vec![1, 3, 5, 7]);
}
