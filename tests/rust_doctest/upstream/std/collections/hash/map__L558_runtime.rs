// Extracted from library/std/src/collections/hash/map.rs:558
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    
    let mut map = HashMap::from([
        ("a", 1),
        ("b", 2),
        ("c", 3),
    ]);
    
    // Update all values
    for (_, val) in map.iter_mut() {
        *val *= 2;
    }
    
    for (key, val) in &map {
        println!("key: {key} val: {val}");
    }
}
