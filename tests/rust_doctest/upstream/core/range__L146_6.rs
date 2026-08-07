// Extracted from library/core/src/range.rs:146
#![allow(unused)]
#![feature(new_range_api)]
fn main() {
    use core::range::Range;

    assert!(!Range::from(3.0..5.0).is_empty());
    assert!( Range::from(3.0..f32::NAN).is_empty());
    assert!( Range::from(f32::NAN..5.0).is_empty());
}
