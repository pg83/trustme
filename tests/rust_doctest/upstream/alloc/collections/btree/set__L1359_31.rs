// Extracted from library/alloc/src/collections/btree/set.rs:1359
#![allow(unused)]
#![feature(btree_cursors)]
extern crate alloc;
fn main() {
    
    use std::collections::BTreeSet;
    use std::ops::Bound;
    
    let mut set = BTreeSet::from([1, 2, 3, 4]);
    
    let mut cursor = set.lower_bound_mut(Bound::Included(&2));
    assert_eq!(cursor.peek_prev(), Some(&1));
    assert_eq!(cursor.peek_next(), Some(&2));
    
    let mut cursor = set.lower_bound_mut(Bound::Excluded(&2));
    assert_eq!(cursor.peek_prev(), Some(&2));
    assert_eq!(cursor.peek_next(), Some(&3));
    
    let mut cursor = set.lower_bound_mut(Bound::Unbounded);
    assert_eq!(cursor.peek_prev(), None);
    assert_eq!(cursor.peek_next(), Some(&1));
}
