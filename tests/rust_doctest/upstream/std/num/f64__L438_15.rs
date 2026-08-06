// Extracted from library/std/src/num/f64.rs:438
#![allow(unused)]
fn main() {
    let one = 1.0_f64;
    // e^1
    let e = one.exp();
    
    // ln(e) - 1 == 0
    let abs_difference = (e.ln() - 1.0).abs();
    
    assert!(abs_difference < 1e-10);
}
