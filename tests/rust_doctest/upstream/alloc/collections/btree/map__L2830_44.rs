// Extracted from library/alloc/src/collections/btree/map.rs:2830
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

    let mut cursor = map.upper_bound_mut(Bound::Included(&3));
    assert_eq!(cursor.peek_prev(), Some((&3, &mut "c")));
    assert_eq!(cursor.peek_next(), Some((&4, &mut "d")));

    let mut cursor = map.upper_bound_mut(Bound::Excluded(&3));
    assert_eq!(cursor.peek_prev(), Some((&2, &mut "b")));
    assert_eq!(cursor.peek_next(), Some((&3, &mut "c")));

    let mut cursor = map.upper_bound_mut(Bound::Unbounded);
    assert_eq!(cursor.peek_prev(), Some((&4, &mut "d")));
    assert_eq!(cursor.peek_next(), None);
}
