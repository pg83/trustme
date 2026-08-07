// Extracted from library/core/src/num/f32.rs:565
#![allow(unused)]
fn main() {
    let f = 7.0f32;
    let inf = f32::INFINITY;
    let neg_inf = f32::NEG_INFINITY;
    let nan = f32::NAN;

    assert!(f.is_finite());

    assert!(!nan.is_finite());
    assert!(!inf.is_finite());
    assert!(!neg_inf.is_finite());
}
