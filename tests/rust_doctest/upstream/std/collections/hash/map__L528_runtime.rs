// Extracted from library/std/src/collections/hash/map.rs:528
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    
    let map = HashMap::from([
        ("a", 1),
        ("b", 2),
        ("c", 3),
    ]);
    
    for (key, val) in map.iter() {
        println!("key: {key} val: {val}");
    }
}
