// Extracted from library/std/src/num/f32.rs:411
#![allow(unused)]
fn main() {
    let f = 2.0f32;
    
    // 2^2 - 4 == 0
    let abs_difference = (f.exp2() - 4.0).abs();
    
    assert!(abs_difference <= 1e-5);
}
