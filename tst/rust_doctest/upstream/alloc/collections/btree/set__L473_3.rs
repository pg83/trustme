// Extracted from library/alloc/src/collections/btree/set.rs:473
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

    let sym_diff: Vec<_> = a.symmetric_difference(&b).cloned().collect();
    assert_eq!(sym_diff, [1, 3]);
}
