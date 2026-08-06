// Extracted from library/std/src/collections/hash/map.rs:433
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    
    let map = HashMap::from([
        ("a", 1),
        ("b", 2),
        ("c", 3),
    ]);
    
    for val in map.values() {
        println!("{val}");
    }
}
