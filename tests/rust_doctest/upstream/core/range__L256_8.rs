// Extracted from library/core/src/range.rs:256
#![allow(unused)]
#![feature(new_range_api)]
fn main() {
    use core::range::RangeInclusive;
    
    assert!(!RangeInclusive::from(3..=5).contains(&2));
    assert!( RangeInclusive::from(3..=5).contains(&3));
    assert!( RangeInclusive::from(3..=5).contains(&4));
    assert!( RangeInclusive::from(3..=5).contains(&5));
    assert!(!RangeInclusive::from(3..=5).contains(&6));
    
    assert!( RangeInclusive::from(3..=3).contains(&3));
    assert!(!RangeInclusive::from(3..=2).contains(&3));
    
    assert!( RangeInclusive::from(0.0..=1.0).contains(&1.0));
    assert!(!RangeInclusive::from(0.0..=1.0).contains(&f32::NAN));
    assert!(!RangeInclusive::from(0.0..=f32::NAN).contains(&0.0));
    assert!(!RangeInclusive::from(f32::NAN..=1.0).contains(&1.0));
}
