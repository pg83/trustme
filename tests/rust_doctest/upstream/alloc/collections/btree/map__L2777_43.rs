// Extracted from library/alloc/src/collections/btree/map.rs:2777
#![allow(unused)]
#![feature(btree_cursors)]
extern crate alloc;
fn main() {
    
    use std::collections::BTreeMap;
    use std::ops::Bound;
    
    let map = BTreeMap::from([
        (1, "a"),
        (2, "b"),
        (3, "c"),
        (4, "d"),
    ]);
    
    let cursor = map.upper_bound(Bound::Included(&3));
    assert_eq!(cursor.peek_prev(), Some((&3, &"c")));
    assert_eq!(cursor.peek_next(), Some((&4, &"d")));
    
    let cursor = map.upper_bound(Bound::Excluded(&3));
    assert_eq!(cursor.peek_prev(), Some((&2, &"b")));
    assert_eq!(cursor.peek_next(), Some((&3, &"c")));
    
    let cursor = map.upper_bound(Bound::Unbounded);
    assert_eq!(cursor.peek_prev(), Some((&4, &"d")));
    assert_eq!(cursor.peek_next(), None);
}
