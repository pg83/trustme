// Extracted from library/alloc/src/collections/btree/set.rs:1120
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;

    let mut a = BTreeSet::new();
    a.insert(1);
    a.insert(2);
    a.insert(3);

    let mut b = BTreeSet::new();
    b.insert(3);
    b.insert(4);
    b.insert(5);

    a.append(&mut b);

    assert_eq!(a.len(), 5);
    assert_eq!(b.len(), 0);

    assert!(a.contains(&1));
    assert!(a.contains(&2));
    assert!(a.contains(&3));
    assert!(a.contains(&4));
    assert!(a.contains(&5));
}
