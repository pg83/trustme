// Extracted from library/std/src/collections/hash/map.rs:355
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    let map: HashMap<i32, i32> = HashMap::with_capacity(100);
    assert!(map.capacity() >= 100);
}
