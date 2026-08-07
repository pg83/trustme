// Extracted from library/alloc/src/collections/btree/set.rs:1316
#![allow(unused)]
#![feature(btree_cursors)]
extern crate alloc;
fn main() {

    use std::collections::BTreeSet;
    use std::ops::Bound;

    let set = BTreeSet::from([1, 2, 3, 4]);

    let cursor = set.lower_bound(Bound::Included(&2));
    assert_eq!(cursor.peek_prev(), Some(&1));
    assert_eq!(cursor.peek_next(), Some(&2));

    let cursor = set.lower_bound(Bound::Excluded(&2));
    assert_eq!(cursor.peek_prev(), Some(&2));
    assert_eq!(cursor.peek_next(), Some(&3));

    let cursor = set.lower_bound(Bound::Unbounded);
    assert_eq!(cursor.peek_prev(), None);
    assert_eq!(cursor.peek_next(), Some(&1));
}
