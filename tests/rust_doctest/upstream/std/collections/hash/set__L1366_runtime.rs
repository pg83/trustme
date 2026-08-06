// Extracted from library/std/src/collections/hash/set.rs:1366
#![allow(unused)]
fn main() {
    use std::collections::HashSet;
    
    let mut a = HashSet::from([1, 2, 3]);
    
    let mut drain = a.drain();
}
