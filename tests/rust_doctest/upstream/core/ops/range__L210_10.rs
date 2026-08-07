// Extracted from library/core/src/ops/range.rs:210
#![allow(unused)]
fn main() {
    assert!(!(3..).contains(&2));
    assert!( (3..).contains(&3));
    assert!( (3..).contains(&1_000_000_000));

    assert!( (0.0..).contains(&0.5));
    assert!(!(0.0..).contains(&f32::NAN));
    assert!(!(f32::NAN..).contains(&0.5));
}
