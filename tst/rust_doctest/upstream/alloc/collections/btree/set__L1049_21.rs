// Extracted from library/alloc/src/collections/btree/set.rs:1049
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;

    let mut set = BTreeSet::new();

    set.insert(2);
    assert_eq!(set.remove(&2), true);
    assert_eq!(set.remove(&2), false);
}
