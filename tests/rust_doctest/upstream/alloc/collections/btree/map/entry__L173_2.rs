// Extracted from library/alloc/src/collections/btree/map/entry.rs:173
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;

    let mut map: BTreeMap<&str, String> = BTreeMap::new();
    let s = "hoho".to_string();

    map.entry("poneyland").or_insert_with(|| s);

    assert_eq!(map["poneyland"], "hoho".to_string());
}
