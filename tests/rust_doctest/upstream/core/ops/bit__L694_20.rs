// Extracted from library/core/src/ops/bit.rs:694
#![allow(unused)]
fn main() {
    let mut x = true;
    x &= false;
    assert_eq!(x, false);
    
    let mut x = true;
    x &= true;
    assert_eq!(x, true);
    
    let mut x: u8 = 5;
    x &= 1;
    assert_eq!(x, 1);
    
    let mut x: u8 = 5;
    x &= 2;
    assert_eq!(x, 0);
}
