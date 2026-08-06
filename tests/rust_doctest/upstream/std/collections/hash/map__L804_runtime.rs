// Extracted from library/std/src/collections/hash/map.rs:804
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    
    let mut map: HashMap<&str, isize> = HashMap::new();
    map.try_reserve(10).expect("why is the test harness OOMing on a handful of bytes?");
}
