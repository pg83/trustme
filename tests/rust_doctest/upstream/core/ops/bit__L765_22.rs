// Extracted from library/core/src/ops/bit.rs:765
#![allow(unused)]
fn main() {
    let mut x = true;
    x |= false;
    assert_eq!(x, true);
    
    let mut x = false;
    x |= false;
    assert_eq!(x, false);
    
    let mut x: u8 = 5;
    x |= 1;
    assert_eq!(x, 5);
    
    let mut x: u8 = 5;
    x |= 2;
    assert_eq!(x, 7);
}
