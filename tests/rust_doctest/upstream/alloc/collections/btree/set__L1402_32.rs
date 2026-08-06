// Extracted from library/alloc/src/collections/btree/set.rs:1402
#![allow(unused)]
#![feature(btree_cursors)]
extern crate alloc;
fn main() {
    
    use std::collections::BTreeSet;
    use std::ops::Bound;
    
    let set = BTreeSet::from([1, 2, 3, 4]);
    
    let cursor = set.upper_bound(Bound::Included(&3));
    assert_eq!(cursor.peek_prev(), Some(&3));
    assert_eq!(cursor.peek_next(), Some(&4));
    
    let cursor = set.upper_bound(Bound::Excluded(&3));
    assert_eq!(cursor.peek_prev(), Some(&2));
    assert_eq!(cursor.peek_next(), Some(&3));
    
    let cursor = set.upper_bound(Bound::Unbounded);
    assert_eq!(cursor.peek_prev(), Some(&4));
    assert_eq!(cursor.peek_next(), None);
}
