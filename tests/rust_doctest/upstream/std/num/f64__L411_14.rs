// Extracted from library/std/src/num/f64.rs:411
#![allow(unused)]
fn main() {
    let f = 2.0_f64;
    
    // 2^2 - 4 == 0
    let abs_difference = (f.exp2() - 4.0).abs();
    
    assert!(abs_difference < 1e-10);
}
