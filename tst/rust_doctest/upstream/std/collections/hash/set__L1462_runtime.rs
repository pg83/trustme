// Extracted from library/std/src/collections/hash/set.rs:1462
#![allow(unused)]
fn main() {
    use std::collections::HashSet;

    let a = HashSet::from([1, 2, 3]);
    let b = HashSet::from([4, 2, 3, 4]);

    let mut intersection = a.symmetric_difference(&b);
}
