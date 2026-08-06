// Extracted from library/std/src/collections/hash/map.rs:2360
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    
    let mut map = HashMap::new();
    let value = "hoho";
    
    map.entry("poneyland").or_insert_with(|| value);
    
    assert_eq!(map["poneyland"], "hoho");
}
