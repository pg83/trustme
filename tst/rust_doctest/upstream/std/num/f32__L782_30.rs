// Extracted from library/std/src/num/f32.rs:782
#![allow(unused)]
fn main() {
    let f = std::f32::consts::FRAC_PI_4;

    // acos(cos(pi/4))
    let abs_difference = (f.cos().acos() - std::f32::consts::FRAC_PI_4).abs();

    assert!(abs_difference <= 1e-6);
}
