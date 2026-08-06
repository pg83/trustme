// Extracted from library/std/src/collections/hash/map.rs:1475
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    
    let mut map = HashMap::from([
        ("a", 1),
    ]);
    let iter = map.iter_mut();
}
