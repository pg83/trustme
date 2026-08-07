// Extracted from library/std/src/collections/hash/set.rs:705
#![allow(unused)]
fn main() {
    use std::collections::HashSet;

    let set = HashSet::from([1, 2, 3]);
    assert_eq!(set.get(&2), Some(&2));
    assert_eq!(set.get(&4), None);
}
