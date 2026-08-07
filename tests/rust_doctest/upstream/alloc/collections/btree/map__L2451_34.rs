// Extracted from library/alloc/src/collections/btree/map.rs:2451
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;

    let map1 = BTreeMap::from([(1, 2), (3, 4)]);
    let map2: BTreeMap<_, _> = [(1, 2), (3, 4)].into();
    assert_eq!(map1, map2);
}
