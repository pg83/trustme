// Extracted from library/alloc/src/collections/btree/map.rs:802
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;
    
    let mut map = BTreeMap::new();
    map.insert(1, "a");
    map.insert(2, "b");
    if let Some(mut entry) = map.first_entry() {
        if *entry.key() > 0 {
            entry.insert("first");
        }
    }
    assert_eq!(*map.get(&1).unwrap(), "first");
    assert_eq!(*map.get(&2).unwrap(), "b");
}
