// Extracted from library/alloc/src/collections/btree/set.rs:504
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;

    let mut a = BTreeSet::new();
    a.insert(1);
    a.insert(2);

    let mut b = BTreeSet::new();
    b.insert(2);
    b.insert(3);

    let intersection: Vec<_> = a.intersection(&b).cloned().collect();
    assert_eq!(intersection, [2]);
}
