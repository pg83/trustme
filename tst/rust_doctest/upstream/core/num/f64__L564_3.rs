// Extracted from library/core/src/num/f64.rs:564
#![allow(unused)]
fn main() {
    let f = 7.0f64;
    let inf: f64 = f64::INFINITY;
    let neg_inf: f64 = f64::NEG_INFINITY;
    let nan: f64 = f64::NAN;

    assert!(f.is_finite());

    assert!(!nan.is_finite());
    assert!(!inf.is_finite());
    assert!(!neg_inf.is_finite());
}
