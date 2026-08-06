// Extracted from library/core/src/ops/range.rs:483
#![allow(unused)]
fn main() {
    assert!(!(3..=5).contains(&2));
    assert!( (3..=5).contains(&3));
    assert!( (3..=5).contains(&4));
    assert!( (3..=5).contains(&5));
    assert!(!(3..=5).contains(&6));
    
    assert!( (3..=3).contains(&3));
    assert!(!(3..=2).contains(&3));
    
    assert!( (0.0..=1.0).contains(&1.0));
    assert!(!(0.0..=1.0).contains(&f32::NAN));
    assert!(!(0.0..=f32::NAN).contains(&0.0));
    assert!(!(f32::NAN..=1.0).contains(&1.0));
}
