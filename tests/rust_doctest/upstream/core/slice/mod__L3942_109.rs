// Extracted from library/core/src/slice/mod.rs:3942
#![allow(unused)]
fn main() {
    let mut slice1 = [0, 0];
    let mut slice2 = [1, 2, 3, 4];
    
    slice1.swap_with_slice(&mut slice2[2..]);
    
    assert_eq!(slice1, [3, 4]);
    assert_eq!(slice2, [1, 2, 0, 0]);
}
