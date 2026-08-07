// Extracted from library/std/src/collections/hash/set.rs:873
#![allow(unused)]
fn main() {
    use std::collections::HashSet;

    let sub = HashSet::from([1, 2]);
    let mut set = HashSet::new();

    assert_eq!(set.is_superset(&sub), false);

    set.insert(0);
    set.insert(1);
    assert_eq!(set.is_superset(&sub), false);

    set.insert(2);
    assert_eq!(set.is_superset(&sub), true);
}
