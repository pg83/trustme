// Extracted from library/alloc/src/collections/btree/set.rs:1160
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;

    let mut a = BTreeSet::new();
    a.insert(1);
    a.insert(2);
    a.insert(3);
    a.insert(17);
    a.insert(41);

    let b = a.split_off(&3);

    assert_eq!(a.len(), 2);
    assert_eq!(b.len(), 3);

    assert!(a.contains(&1));
    assert!(a.contains(&2));

    assert!(b.contains(&3));
    assert!(b.contains(&17));
    assert!(b.contains(&41));
}
