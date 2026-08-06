// Extracted from library/core/src/num/f32.rs:589
#![allow(unused)]
fn main() {
    let min = f32::MIN_POSITIVE; // 1.17549435e-38f32
    let max = f32::MAX;
    let lower_than_min = 1.0e-40_f32;
    let zero = 0.0_f32;
    
    assert!(!min.is_subnormal());
    assert!(!max.is_subnormal());
    
    assert!(!zero.is_subnormal());
    assert!(!f32::NAN.is_subnormal());
    assert!(!f32::INFINITY.is_subnormal());
    // Values between `0` and `min` are Subnormal.
    assert!(lower_than_min.is_subnormal());
}
