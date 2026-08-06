// Extracted from library/alloc/src/collections/btree/map.rs:1231
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;
    use std::ops::Bound::Included;
    
    let mut map = BTreeMap::new();
    map.insert(3, "a");
    map.insert(5, "b");
    map.insert(8, "c");
    for (&key, &value) in map.range((Included(&4), Included(&8))) {
        println!("{key}: {value}");
    }
    assert_eq!(Some((&5, &"b")), map.range(4..).next());
}
