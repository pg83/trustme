// Extracted from library/alloc/src/collections/btree/map/entry.rs:344
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;
    use std::collections::btree_map::Entry;
    
    let mut map: BTreeMap<&str, usize> = BTreeMap::new();
    
    if let Entry::Vacant(v) = map.entry("poneyland") {
        v.into_key();
    }
}
