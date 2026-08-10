// Extracted from library/alloc/src/collections/btree/map.rs:2535
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;

    let mut a = BTreeMap::new();
    a.insert(2, "b");
    a.insert(1, "a");

    let keys: Vec<_> = a.keys().cloned().collect();
    assert_eq!(keys, [1, 2]);
}
