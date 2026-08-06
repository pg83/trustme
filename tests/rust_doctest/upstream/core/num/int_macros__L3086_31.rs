// Extracted from library/core/src/num/int_macros.rs:3086
#![allow(unused)]
#![feature(int_roundings)]
fn main() {
    
    let b = 3;
    
    assert_eq!(a.div_ceil(b), 3);
    assert_eq!(a.div_ceil(-b), -2);
    assert_eq!((-a).div_ceil(b), -2);
    assert_eq!((-a).div_ceil(-b), 3);
}
