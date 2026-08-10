// Extracted from library/core/src/range.rs:104
#![allow(unused)]
#![feature(new_range_api)]
fn main() {
    use core::range::Range;

    assert!(!Range::from(3..5).contains(&2));
    assert!( Range::from(3..5).contains(&3));
    assert!( Range::from(3..5).contains(&4));
    assert!(!Range::from(3..5).contains(&5));

    assert!(!Range::from(3..3).contains(&3));
    assert!(!Range::from(3..2).contains(&3));

    assert!( Range::from(0.0..1.0).contains(&0.5));
    assert!(!Range::from(0.0..1.0).contains(&f32::NAN));
    assert!(!Range::from(0.0..f32::NAN).contains(&0.5));
    assert!(!Range::from(f32::NAN..1.0).contains(&0.5));
}
