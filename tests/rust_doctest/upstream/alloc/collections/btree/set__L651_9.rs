// Extracted from library/alloc/src/collections/btree/set.rs:651
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;

    let a = BTreeSet::from([1, 2, 3]);
    let mut b = BTreeSet::new();

    assert_eq!(a.is_disjoint(&b), true);
    b.insert(4);
    assert_eq!(a.is_disjoint(&b), true);
    b.insert(1);
    assert_eq!(a.is_disjoint(&b), false);
}
