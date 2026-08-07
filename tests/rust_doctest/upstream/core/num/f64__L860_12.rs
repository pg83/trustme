// Extracted from library/core/src/num/f64.rs:860
#![allow(unused)]
fn main() {
    let angle = std::f64::consts::PI;

    let abs_difference = (angle.to_degrees() - 180.0).abs();

    assert!(abs_difference < 1e-10);
}
