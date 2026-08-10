// Extracted from library/alloc/src/collections/btree/set.rs:812
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;

    let mut set = BTreeSet::new();
    assert_eq!(set.last(), None);
    set.insert(1);
    assert_eq!(set.last(), Some(&1));
    set.insert(2);
    assert_eq!(set.last(), Some(&2));
}
