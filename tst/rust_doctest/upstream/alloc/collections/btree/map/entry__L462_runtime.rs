// Extracted from library/alloc/src/collections/btree/map/entry.rs:462
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;
    use std::collections::btree_map::Entry;

    let mut map: BTreeMap<&str, usize> = BTreeMap::new();
    map.entry("poneyland").or_insert(12);

    if let Entry::Occupied(o) = map.entry("poneyland") {
        // We delete the entry from the map.
        o.remove_entry();
    }

    // If now try to get the value, it will panic:
    // println!("{}", map["poneyland"]);
}
