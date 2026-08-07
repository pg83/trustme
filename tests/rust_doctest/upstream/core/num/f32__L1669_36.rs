// Extracted from library/core/src/num/f32.rs:1669
#![allow(unused)]
#![feature(core_float_math)]
fn main() {

    use core::f32;

    let f = 3.3_f32;
    let g = -3.3_f32;
    let h = 3.5_f32;
    let i = 4.5_f32;

    assert_eq!(f32::math::round_ties_even(f), 3.0);
    assert_eq!(f32::math::round_ties_even(g), -3.0);
    assert_eq!(f32::math::round_ties_even(h), 4.0);
    assert_eq!(f32::math::round_ties_even(i), 4.0);
}
