// Extracted from library/alloc/src/collections/btree/map.rs:863
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;
    
    let mut map = BTreeMap::new();
    map.insert(1, "b");
    map.insert(2, "a");
    assert_eq!(map.last_key_value(), Some((&2, &"a")));
}
