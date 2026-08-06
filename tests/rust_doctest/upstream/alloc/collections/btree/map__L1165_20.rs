// Extracted from library/alloc/src/collections/btree/map.rs:1165
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;
    
    let mut a = BTreeMap::new();
    a.insert(1, "a");
    a.insert(2, "b");
    a.insert(3, "c"); // Note: Key (3) also present in b.
    
    let mut b = BTreeMap::new();
    b.insert(3, "d"); // Note: Key (3) also present in a.
    b.insert(4, "e");
    b.insert(5, "f");
    
    a.append(&mut b);
    
    assert_eq!(a.len(), 5);
    assert_eq!(b.len(), 0);
    
    assert_eq!(a[&1], "a");
    assert_eq!(a[&2], "b");
    assert_eq!(a[&3], "d"); // Note: "c" has been overwritten.
    assert_eq!(a[&4], "e");
    assert_eq!(a[&5], "f");
}
