// Extracted from library/alloc/src/collections/btree/map/entry.rs:307
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;

    let mut map: BTreeMap<&str, Option<usize>> = BTreeMap::new();
    map.entry("poneyland").or_default();

    assert_eq!(map["poneyland"], None);
}
