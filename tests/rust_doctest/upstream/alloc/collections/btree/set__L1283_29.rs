// Extracted from library/alloc/src/collections/btree/set.rs:1283
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;
    
    let mut v = BTreeSet::new();
    assert!(v.is_empty());
    v.insert(1);
    assert!(!v.is_empty());
}
