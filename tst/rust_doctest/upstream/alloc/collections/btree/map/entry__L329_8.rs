// Extracted from library/alloc/src/collections/btree/map/entry.rs:329
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;

    let mut map: BTreeMap<&str, usize> = BTreeMap::new();
    assert_eq!(map.entry("poneyland").key(), &"poneyland");
}
