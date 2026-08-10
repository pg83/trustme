// Extracted from library/alloc/src/collections/btree/map.rs:617
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;

    let mut map = BTreeMap::new();

    // entries can now be inserted into the empty map
    map.insert(1, "a");
}
