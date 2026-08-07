// Extracted from library/std/src/num/f32.rs:543
#![allow(unused)]
fn main() {
    let ten = 10.0f32;

    // log10(10) - 1 == 0
    let abs_difference = (ten.log10() - 1.0).abs();

    assert!(abs_difference <= 1e-6);
}
