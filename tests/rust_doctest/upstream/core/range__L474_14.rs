// Extracted from library/core/src/range.rs:474
#![allow(unused)]
#![feature(new_range_api)]
fn main() {
    use core::range::RangeFrom;

    assert!(!RangeFrom::from(3..).contains(&2));
    assert!( RangeFrom::from(3..).contains(&3));
    assert!( RangeFrom::from(3..).contains(&1_000_000_000));

    assert!( RangeFrom::from(0.0..).contains(&0.5));
    assert!(!RangeFrom::from(0.0..).contains(&f32::NAN));
    assert!(!RangeFrom::from(f32::NAN..).contains(&0.5));
}
