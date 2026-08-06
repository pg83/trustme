// Extracted from library/std/src/num/f64.rs:543
#![allow(unused)]
fn main() {
    let hundred = 100.0_f64;
    
    // log10(100) - 2 == 0
    let abs_difference = (hundred.log10() - 2.0).abs();
    
    assert!(abs_difference < 1e-10);
}
