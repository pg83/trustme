// Extracted from library/alloc/src/collections/btree/map/entry.rs:278
#![allow(unused)]
#![feature(btree_entry_insert)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;

    let mut map: BTreeMap<&str, String> = BTreeMap::new();
    let entry = map.entry("poneyland").insert_entry("hoho".to_string());

    assert_eq!(entry.key(), &"poneyland");
}
