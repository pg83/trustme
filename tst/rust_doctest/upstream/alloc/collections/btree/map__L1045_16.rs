// Extracted from library/alloc/src/collections/btree/map.rs:1045
#![allow(unused)]
#![feature(map_try_insert)]
extern crate alloc;
fn main() {

    use std::collections::BTreeMap;

    let mut map = BTreeMap::new();
    assert_eq!(map.try_insert(37, "a").unwrap(), &"a");

    let err = map.try_insert(37, "b").unwrap_err();
    assert_eq!(err.entry.key(), &37);
    assert_eq!(err.entry.get(), &"a");
    assert_eq!(err.value, "b");
}
