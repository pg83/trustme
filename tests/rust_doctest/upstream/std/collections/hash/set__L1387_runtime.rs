// Extracted from library/std/src/collections/hash/set.rs:1387
#![allow(unused)]
fn main() {
    use std::collections::HashSet;
    
    let mut a = HashSet::from([1, 2, 3]);
    
    let mut extract_ifed = a.extract_if(|v| v % 2 == 0);
}
