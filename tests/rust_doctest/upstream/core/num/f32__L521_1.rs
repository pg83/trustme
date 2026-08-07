// Extracted from library/core/src/num/f32.rs:521
#![allow(unused)]
fn main() {
    let nan = f32::NAN;
    let f = 7.0_f32;

    assert!(nan.is_nan());
    assert!(!f.is_nan());
}
