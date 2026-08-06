// Extracted from library/alloc/src/collections/btree/set.rs:581
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;
    
    let mut v = BTreeSet::new();
    v.insert(1);
    v.clear();
    assert!(v.is_empty());
}
