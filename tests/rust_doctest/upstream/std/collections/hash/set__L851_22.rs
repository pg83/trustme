// Extracted from library/std/src/collections/hash/set.rs:851
#![allow(unused)]
fn main() {
    use std::collections::HashSet;

    let sup = HashSet::from([1, 2, 3]);
    let mut set = HashSet::new();

    assert_eq!(set.is_subset(&sup), true);
    set.insert(2);
    assert_eq!(set.is_subset(&sup), true);
    set.insert(4);
    assert_eq!(set.is_subset(&sup), false);
}
