// Extracted from library/core/src/num/int_macros.rs:3044
#![allow(unused)]
#![feature(int_roundings)]
fn main() {
    
    let b = 3;
    
    assert_eq!(a.div_floor(b), 2);
    assert_eq!(a.div_floor(-b), -3);
    assert_eq!((-a).div_floor(b), -3);
    assert_eq!((-a).div_floor(-b), 2);
}
