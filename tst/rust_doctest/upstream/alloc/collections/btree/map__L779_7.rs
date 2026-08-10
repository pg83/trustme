// Extracted from library/alloc/src/collections/btree/map.rs:779
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;

    let mut map = BTreeMap::new();
    assert_eq!(map.first_key_value(), None);
    map.insert(1, "b");
    map.insert(2, "a");
    assert_eq!(map.first_key_value(), Some((&1, &"b")));
}
