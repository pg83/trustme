// Extracted from library/alloc/src/collections/btree/set.rs:557
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;

    let mut a = BTreeSet::new();
    a.insert(1);

    let mut b = BTreeSet::new();
    b.insert(2);

    let union: Vec<_> = a.union(&b).cloned().collect();
    assert_eq!(union, [1, 2]);
}
