// Extracted from library/std/src/num/f64.rs:510
#![allow(unused)]
fn main() {
    let four = 4.0_f64;

    // log2(4) - 2 == 0
    let abs_difference = (four.log2() - 2.0).abs();

    assert!(abs_difference < 1e-10);
}
