// Extracted from library/std/src/collections/hash/map.rs:846
#![allow(unused)]
fn main() {
    use std::collections::HashMap;

    let mut map: HashMap<i32, i32> = HashMap::with_capacity(100);
    map.insert(1, 2);
    map.insert(3, 4);
    assert!(map.capacity() >= 100);
    map.shrink_to(10);
    assert!(map.capacity() >= 10);
    map.shrink_to(0);
    assert!(map.capacity() >= 2);
}
