// Extracted from library/std/src/num/f64.rs:619
#![allow(unused)]
fn main() {
    let x = 8.0_f64;

    // x^(1/3) - 2 == 0
    let abs_difference = (x.cbrt() - 2.0).abs();

    assert!(abs_difference < 1e-10);
}
