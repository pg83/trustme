// Extracted from library/std/src/num/f32.rs:510
#![allow(unused)]
fn main() {
    let two = 2.0f32;

    // log2(2) - 1 == 0
    let abs_difference = (two.log2() - 1.0).abs();

    assert!(abs_difference <= 1e-6);
}
