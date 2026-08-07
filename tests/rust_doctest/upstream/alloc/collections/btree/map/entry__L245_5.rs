// Extracted from library/alloc/src/collections/btree/map/entry.rs:245
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;

    let mut map: BTreeMap<&str, usize> = BTreeMap::new();

    map.entry("poneyland")
       .and_modify(|e| { *e += 1 })
       .or_insert(42);
    assert_eq!(map["poneyland"], 42);

    map.entry("poneyland")
       .and_modify(|e| { *e += 1 })
       .or_insert(42);
    assert_eq!(map["poneyland"], 43);
}
