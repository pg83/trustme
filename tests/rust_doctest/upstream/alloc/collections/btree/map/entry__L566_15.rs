// Extracted from library/alloc/src/collections/btree/map/entry.rs:566
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;
    use std::collections::btree_map::Entry;

    let mut map: BTreeMap<&str, usize> = BTreeMap::new();
    map.entry("poneyland").or_insert(12);

    if let Entry::Occupied(mut o) = map.entry("poneyland") {
        assert_eq!(o.insert(15), 12);
    }
    assert_eq!(map["poneyland"], 15);
}
