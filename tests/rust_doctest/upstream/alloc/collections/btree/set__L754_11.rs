// Extracted from library/alloc/src/collections/btree/set.rs:754
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;

    let sub = BTreeSet::from([1, 2]);
    let mut set = BTreeSet::new();

    assert_eq!(set.is_superset(&sub), false);

    set.insert(0);
    set.insert(1);
    assert_eq!(set.is_superset(&sub), false);

    set.insert(2);
    assert_eq!(set.is_superset(&sub), true);
}
