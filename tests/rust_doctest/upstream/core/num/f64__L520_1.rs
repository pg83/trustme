// Extracted from library/core/src/num/f64.rs:520
#![allow(unused)]
fn main() {
    let nan = f64::NAN;
    let f = 7.0_f64;
    
    assert!(nan.is_nan());
    assert!(!f.is_nan());
}
