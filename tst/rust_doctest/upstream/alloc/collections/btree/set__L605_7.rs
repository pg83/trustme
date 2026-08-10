// Extracted from library/alloc/src/collections/btree/set.rs:605
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;

    let set = BTreeSet::from([1, 2, 3]);
    assert_eq!(set.contains(&1), true);
    assert_eq!(set.contains(&4), false);
}
