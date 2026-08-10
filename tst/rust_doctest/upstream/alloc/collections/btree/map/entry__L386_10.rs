// Extracted from library/alloc/src/collections/btree/map/entry.rs:386
#![allow(unused)]
#![feature(btree_entry_insert)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;
    use std::collections::btree_map::Entry;

    let mut map: BTreeMap<&str, u32> = BTreeMap::new();

    if let Entry::Vacant(o) = map.entry("poneyland") {
        let entry = o.insert_entry(37);
        assert_eq!(entry.get(), &37);
    }
    assert_eq!(map["poneyland"], 37);
}
