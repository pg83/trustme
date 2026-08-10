// Extracted from library/std/src/num/f64.rs:782
#![allow(unused)]
fn main() {
    let f = std::f64::consts::FRAC_PI_4;

    // acos(cos(pi/4))
    let abs_difference = (f.cos().acos() - std::f64::consts::FRAC_PI_4).abs();

    assert!(abs_difference < 1e-10);
}
