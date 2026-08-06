// Extracted from library/alloc/src/collections/btree/map/entry.rs:486
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;
    use std::collections::btree_map::Entry;
    
    let mut map: BTreeMap<&str, usize> = BTreeMap::new();
    map.entry("poneyland").or_insert(12);
    
    if let Entry::Occupied(o) = map.entry("poneyland") {
        assert_eq!(o.get(), &12);
    }
}
