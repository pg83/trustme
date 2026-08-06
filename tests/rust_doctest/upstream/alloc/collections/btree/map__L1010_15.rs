// Extracted from library/alloc/src/collections/btree/map.rs:1010
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;
    
    let mut map = BTreeMap::new();
    assert_eq!(map.insert(37, "a"), None);
    assert_eq!(map.is_empty(), false);
    
    map.insert(37, "b");
    assert_eq!(map.insert(37, "c"), Some("b"));
    assert_eq!(map[&37], "c");
}
