// Extracted from library/core/src/num/f32.rs:540
#![allow(unused)]
fn main() {
    let f = 7.0f32;
    let inf = f32::INFINITY;
    let neg_inf = f32::NEG_INFINITY;
    let nan = f32::NAN;

    assert!(!f.is_infinite());
    assert!(!nan.is_infinite());

    assert!(inf.is_infinite());
    assert!(neg_inf.is_infinite());
}
