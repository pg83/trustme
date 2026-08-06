// Extracted from library/alloc/src/collections/btree/map/entry.rs:364
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;
    use std::collections::btree_map::Entry;
    
    let mut map: BTreeMap<&str, u32> = BTreeMap::new();
    
    if let Entry::Vacant(o) = map.entry("poneyland") {
        o.insert(37);
    }
    assert_eq!(map["poneyland"], 37);
}
