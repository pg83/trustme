// Extracted from library/alloc/src/collections/btree/set.rs:895
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;

    let mut set = BTreeSet::new();

    assert_eq!(set.insert(2), true);
    assert_eq!(set.insert(2), false);
    assert_eq!(set.len(), 1);
}
