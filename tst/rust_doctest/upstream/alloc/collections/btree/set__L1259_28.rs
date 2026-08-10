// Extracted from library/alloc/src/collections/btree/set.rs:1259
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;

    let mut v = BTreeSet::new();
    assert_eq!(v.len(), 0);
    v.insert(1);
    assert_eq!(v.len(), 1);
}
