// Extracted from library/std/src/num/f64.rs:579
#![allow(unused)]
fn main() {
    let x = 3.0_f64;
    let y = -3.0_f64;

    let abs_difference_x = (x.abs_sub(1.0) - 2.0).abs();
    let abs_difference_y = (y.abs_sub(1.0) - 0.0).abs();

    assert!(abs_difference_x < 1e-10);
    assert!(abs_difference_y < 1e-10);
}
