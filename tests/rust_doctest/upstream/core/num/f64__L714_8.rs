// Extracted from library/core/src/num/f64.rs:714
#![allow(unused)]
fn main() {
    let f = 7.0_f64;
    let g = -7.0_f64;
    
    assert!(!f.is_sign_negative());
    assert!(g.is_sign_negative());
}
