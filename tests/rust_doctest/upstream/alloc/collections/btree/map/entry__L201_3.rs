// Extracted from library/alloc/src/collections/btree/map/entry.rs:201
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;
    
    let mut map: BTreeMap<&str, usize> = BTreeMap::new();
    
    map.entry("poneyland").or_insert_with_key(|key| key.chars().count());
    
    assert_eq!(map["poneyland"], 9);
}
