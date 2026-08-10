// Extracted from library/alloc/src/collections/btree/set.rs:1239
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;

    let set = BTreeSet::from([3, 1, 2]);
    let mut set_iter = set.iter();
    assert_eq!(set_iter.next(), Some(&1));
    assert_eq!(set_iter.next(), Some(&2));
    assert_eq!(set_iter.next(), Some(&3));
    assert_eq!(set_iter.next(), None);
}
