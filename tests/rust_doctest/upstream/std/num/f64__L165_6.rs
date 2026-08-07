// Extracted from library/std/src/num/f64.rs:165
#![allow(unused)]
fn main() {
    let x = 3.6_f64;
    let y = -3.6_f64;
    let abs_difference_x = (x.fract() - 0.6).abs();
    let abs_difference_y = (y.fract() - (-0.6)).abs();

    assert!(abs_difference_x < 1e-10);
    assert!(abs_difference_y < 1e-10);
}
