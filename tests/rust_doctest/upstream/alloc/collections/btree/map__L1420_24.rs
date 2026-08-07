// Extracted from library/alloc/src/collections/btree/map.rs:1420
#![allow(unused)]
#![feature(btree_extract_if)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;

    // Splitting a map into even and odd keys, reusing the original map:
    let mut map: BTreeMap<i32, i32> = (0..8).map(|x| (x, x)).collect();
    let evens: BTreeMap<_, _> = map.extract_if(.., |k, _v| k % 2 == 0).collect();
    let odds = map;
    assert_eq!(evens.keys().copied().collect::<Vec<_>>(), [0, 2, 4, 6]);
    assert_eq!(odds.keys().copied().collect::<Vec<_>>(), [1, 3, 5, 7]);

    // Splitting a map into low and high halves, reusing the original map:
    let mut map: BTreeMap<i32, i32> = (0..8).map(|x| (x, x)).collect();
    let low: BTreeMap<_, _> = map.extract_if(0..4, |_k, _v| true).collect();
    let high = map;
    assert_eq!(low.keys().copied().collect::<Vec<_>>(), [0, 1, 2, 3]);
    assert_eq!(high.keys().copied().collect::<Vec<_>>(), [4, 5, 6, 7]);
}
