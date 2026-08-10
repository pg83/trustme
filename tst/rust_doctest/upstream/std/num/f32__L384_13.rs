// Extracted from library/std/src/num/f32.rs:384
#![allow(unused)]
fn main() {
    let one = 1.0f32;
    // e^1
    let e = one.exp();

    // ln(e) - 1 == 0
    let abs_difference = (e.ln() - 1.0).abs();

    assert!(abs_difference <= 1e-6);
}
