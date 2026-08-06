// Extracted from library/alloc/src/collections/btree/map.rs:839
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;
    
    let mut map = BTreeMap::new();
    map.insert(1, "a");
    map.insert(2, "b");
    while let Some((key, _val)) = map.pop_first() {
        assert!(map.iter().all(|(k, _v)| *k > key));
    }
    assert!(map.is_empty());
}
