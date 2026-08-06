// Extracted from library/core/src/num/f64.rs:881
#![allow(unused)]
fn main() {
    let angle = 180.0_f64;
    
    let abs_difference = (angle.to_radians() - std::f64::consts::PI).abs();
    
    assert!(abs_difference < 1e-10);
}
