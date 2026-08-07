// Extracted from library/alloc/src/collections/btree/map.rs:639
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;

    let mut a = BTreeMap::new();
    a.insert(1, "a");
    a.clear();
    assert!(a.is_empty());
}
