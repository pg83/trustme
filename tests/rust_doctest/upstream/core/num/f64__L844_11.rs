// Extracted from library/core/src/num/f64.rs:844
#![allow(unused)]
fn main() {
    let x = 2.0_f64;
    let abs_difference = (x.recip() - (1.0 / x)).abs();
    
    assert!(abs_difference < 1e-10);
}
