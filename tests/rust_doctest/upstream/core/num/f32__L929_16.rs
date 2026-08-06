// Extracted from library/core/src/num/f32.rs:929
#![allow(unused)]
#![feature(float_minimum_maximum)]
fn main() {
    let x = 1.0f32;
    let y = 2.0f32;
    
    assert_eq!(x.maximum(y), y);
    assert!(x.maximum(f32::NAN).is_nan());
}
