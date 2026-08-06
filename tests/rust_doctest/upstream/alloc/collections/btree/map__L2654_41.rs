// Extracted from library/alloc/src/collections/btree/map.rs:2654
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
    
    let cursor = map.lower_bound(Bound::Included(&2));
    assert_eq!(cursor.peek_prev(), Some((&1, &"a")));
    assert_eq!(cursor.peek_next(), Some((&2, &"b")));
    
    let cursor = map.lower_bound(Bound::Excluded(&2));
    assert_eq!(cursor.peek_prev(), Some((&2, &"b")));
    assert_eq!(cursor.peek_next(), Some((&3, &"c")));
    
    let cursor = map.lower_bound(Bound::Unbounded);
    assert_eq!(cursor.peek_prev(), None);
    assert_eq!(cursor.peek_next(), Some((&1, &"a")));
}
