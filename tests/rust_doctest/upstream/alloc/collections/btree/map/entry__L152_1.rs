// Extracted from library/alloc/src/collections/btree/map/entry.rs:152
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;
    
    let mut map: BTreeMap<&str, usize> = BTreeMap::new();
    map.entry("poneyland").or_insert(12);
    
    assert_eq!(map["poneyland"], 12);
}
