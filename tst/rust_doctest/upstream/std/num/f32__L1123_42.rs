// Extracted from library/std/src/num/f32.rs:1123
#![allow(unused)]
fn main() {
    let e = std::f32::consts::E;
    let f = e.tanh().atanh();

    let abs_difference = (f - e).abs();

    assert!(abs_difference <= 1e-5);
}
