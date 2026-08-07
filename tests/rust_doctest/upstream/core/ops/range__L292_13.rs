// Extracted from library/core/src/ops/range.rs:292
#![allow(unused)]
fn main() {
    assert!( (..5).contains(&-1_000_000_000));
    assert!( (..5).contains(&4));
    assert!(!(..5).contains(&5));

    assert!( (..1.0).contains(&0.5));
    assert!(!(..1.0).contains(&f32::NAN));
    assert!(!(..f32::NAN).contains(&0.5));
}
