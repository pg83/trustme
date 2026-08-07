// Extracted from library/alloc/src/collections/btree/map/entry.rs:512
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;
    use std::collections::btree_map::Entry;

    let mut map: BTreeMap<&str, usize> = BTreeMap::new();
    map.entry("poneyland").or_insert(12);

    assert_eq!(map["poneyland"], 12);
    if let Entry::Occupied(mut o) = map.entry("poneyland") {
        *o.get_mut() += 10;
        assert_eq!(*o.get(), 22);

        // We can use the same Entry multiple times.
        *o.get_mut() += 2;
    }
    assert_eq!(map["poneyland"], 24);
}
