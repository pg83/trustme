// Extracted from library/std/src/num/f64.rs:752
#![allow(unused)]
fn main() {
    let f = std::f64::consts::FRAC_PI_2;

    // asin(sin(pi/2))
    let abs_difference = (f.sin().asin() - std::f64::consts::FRAC_PI_2).abs();

    assert!(abs_difference < 1e-7);
}
