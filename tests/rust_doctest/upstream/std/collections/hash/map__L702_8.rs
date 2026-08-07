// Extracted from library/std/src/collections/hash/map.rs:702
#![allow(unused)]
fn main() {
    use std::collections::HashMap;

    let mut map: HashMap<i32, i32> = (0..8).map(|x| (x, x*10)).collect();
    map.retain(|&k, _| k % 2 == 0);
    assert_eq!(map.len(), 4);
}
