// Extracted from library/alloc/src/collections/btree/map.rs:885
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;

    let mut map = BTreeMap::new();
    map.insert(1, "a");
    map.insert(2, "b");
    if let Some(mut entry) = map.last_entry() {
        if *entry.key() > 0 {
            entry.insert("last");
        }
    }
    assert_eq!(*map.get(&1).unwrap(), "a");
    assert_eq!(*map.get(&2).unwrap(), "last");
}
