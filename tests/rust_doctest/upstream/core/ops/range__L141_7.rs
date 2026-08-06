// Extracted from library/core/src/ops/range.rs:141
#![allow(unused)]
fn main() {
    assert!(!(3.0..5.0).is_empty());
    assert!( (3.0..f32::NAN).is_empty());
    assert!( (f32::NAN..5.0).is_empty());
}
