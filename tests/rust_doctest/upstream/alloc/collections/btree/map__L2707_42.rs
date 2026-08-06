// Extracted from library/alloc/src/collections/btree/map.rs:2707
#![allow(unused)]
#![feature(btree_cursors)]
extern crate alloc;
fn main() {
    
    use std::collections::BTreeMap;
    use std::ops::Bound;
    
    let mut map = BTreeMap::from([
        (1, "a"),
        (2, "b"),
        (3, "c"),
        (4, "d"),
    ]);
    
    let mut cursor = map.lower_bound_mut(Bound::Included(&2));
    assert_eq!(cursor.peek_prev(), Some((&1, &mut "a")));
    assert_eq!(cursor.peek_next(), Some((&2, &mut "b")));
    
    let mut cursor = map.lower_bound_mut(Bound::Excluded(&2));
    assert_eq!(cursor.peek_prev(), Some((&2, &mut "b")));
    assert_eq!(cursor.peek_next(), Some((&3, &mut "c")));
    
    let mut cursor = map.lower_bound_mut(Bound::Unbounded);
    assert_eq!(cursor.peek_prev(), None);
    assert_eq!(cursor.peek_next(), Some((&1, &mut "a")));
}
