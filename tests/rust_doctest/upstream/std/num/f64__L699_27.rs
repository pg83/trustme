// Extracted from library/std/src/num/f64.rs:699
#![allow(unused)]
fn main() {
    let x = 2.0 * std::f64::consts::PI;

    let abs_difference = (x.cos() - 1.0).abs();

    assert!(abs_difference < 1e-10);
}
