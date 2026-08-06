// Extracted from library/std/src/collections/hash/map.rs:1676
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    
    let mut map = HashMap::from([
        ("a", 1),
    ]);
    let iter = map.extract_if(|_k, v| *v % 2 == 0);
}
