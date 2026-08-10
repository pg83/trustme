// Extracted from library/std/src/collections/hash/map.rs:1407
#![allow(unused)]
fn main() {
    use std::collections::HashMap;

    let map1 = HashMap::from([(1, 2), (3, 4)]);
    let map2: HashMap<_, _> = [(1, 2), (3, 4)].into();
    assert_eq!(map1, map2);
}
