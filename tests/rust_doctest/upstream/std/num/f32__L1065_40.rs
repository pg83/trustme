// Extracted from library/std/src/num/f32.rs:1065
#![allow(unused)]
fn main() {
    let x = 1.0f32;
    let f = x.sinh().asinh();
    
    let abs_difference = (f - x).abs();
    
    assert!(abs_difference <= 1e-7);
}
