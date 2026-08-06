// Extracted from library/alloc/src/collections/btree/map.rs:1353
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;
    
    let mut a = BTreeMap::new();
    a.insert(1, "a");
    a.insert(2, "b");
    a.insert(3, "c");
    a.insert(17, "d");
    a.insert(41, "e");
    
    let b = a.split_off(&3);
    
    assert_eq!(a.len(), 2);
    assert_eq!(b.len(), 3);
    
    assert_eq!(a[&1], "a");
    assert_eq!(a[&2], "b");
    
    assert_eq!(b[&3], "c");
    assert_eq!(b[&17], "d");
    assert_eq!(b[&41], "e");
}
