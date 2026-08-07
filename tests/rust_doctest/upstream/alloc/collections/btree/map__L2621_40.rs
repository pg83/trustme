// Extracted from library/alloc/src/collections/btree/map.rs:2621
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;

    let mut a = BTreeMap::new();
    assert!(a.is_empty());
    a.insert(1, "a");
    assert!(!a.is_empty());
}
