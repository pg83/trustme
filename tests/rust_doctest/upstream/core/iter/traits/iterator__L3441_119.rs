// Extracted from library/core/src/iter/traits/iterator.rs:3441
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];
    
    let v_cloned: Vec<_> = a.iter().cloned().collect();
    
    // cloned is the same as .map(|&x| x), for integers
    let v_map: Vec<_> = a.iter().map(|&x| x).collect();
    
    assert_eq!(v_cloned, [1, 2, 3]);
    assert_eq!(v_map, [1, 2, 3]);
}
