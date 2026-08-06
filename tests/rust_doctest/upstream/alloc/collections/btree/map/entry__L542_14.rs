// Extracted from library/alloc/src/collections/btree/map/entry.rs:542
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;
    use std::collections::btree_map::Entry;
    
    let mut map: BTreeMap<&str, usize> = BTreeMap::new();
    map.entry("poneyland").or_insert(12);
    
    assert_eq!(map["poneyland"], 12);
    if let Entry::Occupied(o) = map.entry("poneyland") {
        *o.into_mut() += 10;
    }
    assert_eq!(map["poneyland"], 22);
}
