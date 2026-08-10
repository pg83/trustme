// Extracted from library/std/src/collections/hash/set.rs:1098
#![allow(unused)]
fn main() {
    use std::collections::HashSet;

    let set1 = HashSet::from([1, 2, 3, 4]);
    let set2: HashSet<_> = [1, 2, 3, 4].into();
    assert_eq!(set1, set2);
}
