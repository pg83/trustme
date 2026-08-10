// Extracted from library/std/src/num/f64.rs:477
#![allow(unused)]
fn main() {
    let twenty_five = 25.0_f64;

    // log5(25) - 2 == 0
    let abs_difference = (twenty_five.log(5.0) - 2.0).abs();

    assert!(abs_difference < 1e-10);
}
