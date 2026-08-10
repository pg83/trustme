// Extracted from library/std/src/num/f32.rs:881
#![allow(unused)]
fn main() {
    let x = std::f32::consts::FRAC_PI_4;
    let f = x.sin_cos();

    let abs_difference_0 = (f.0 - x.sin()).abs();
    let abs_difference_1 = (f.1 - x.cos()).abs();

    assert!(abs_difference_0 <= 1e-6);
    assert!(abs_difference_1 <= 1e-6);
}
