// Extracted from library/std/src/num/f64.rs:1093
#![allow(unused)]
fn main() {
    let x = 1.0_f64;
    let f = x.cosh().acosh();
    
    let abs_difference = (f - x).abs();
    
    assert!(abs_difference < 1.0e-10);
}
