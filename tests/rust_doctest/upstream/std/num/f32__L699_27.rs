// Extracted from library/std/src/num/f32.rs:699
#![allow(unused)]
fn main() {
    let x = 2.0 * std::f32::consts::PI;
    
    let abs_difference = (x.cos() - 1.0).abs();
    
    assert!(abs_difference <= 1e-6);
}
