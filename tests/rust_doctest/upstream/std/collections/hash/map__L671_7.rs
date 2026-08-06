// Extracted from library/std/src/collections/hash/map.rs:671
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    
    let mut map: HashMap<i32, i32> = (0..8).map(|x| (x, x)).collect();
    let extracted: HashMap<i32, i32> = map.extract_if(|k, _v| k % 2 == 0).collect();
    
    let mut evens = extracted.keys().copied().collect::<Vec<_>>();
    let mut odds = map.keys().copied().collect::<Vec<_>>();
    evens.sort();
    odds.sort();
    
    assert_eq!(evens, vec![0, 2, 4, 6]);
    assert_eq!(odds, vec![1, 3, 5, 7]);
}
