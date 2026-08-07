// Extracted from library/alloc/src/collections/btree/set.rs:677
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;

    let sup = BTreeSet::from([1, 2, 3]);
    let mut set = BTreeSet::new();

    assert_eq!(set.is_subset(&sup), true);
    set.insert(2);
    assert_eq!(set.is_subset(&sup), true);
    set.insert(4);
    assert_eq!(set.is_subset(&sup), false);
}
