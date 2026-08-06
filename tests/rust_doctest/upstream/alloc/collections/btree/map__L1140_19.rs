// Extracted from library/alloc/src/collections/btree/map.rs:1140
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;
    
    let mut map: BTreeMap<i32, i32> = (0..8).map(|x| (x, x*10)).collect();
    // Keep only the elements with even-numbered keys.
    map.retain(|&k, _| k % 2 == 0);
    assert!(map.into_iter().eq(vec![(0, 0), (2, 20), (4, 40), (6, 60)]));
}
