// Extracted from library/core/src/ops/range.rs:804
#![allow(unused)]
fn main() {
    assert!( (3..5).contains(&4));
    assert!(!(3..5).contains(&2));
    
    assert!( (0.0..1.0).contains(&0.5));
    assert!(!(0.0..1.0).contains(&f32::NAN));
    assert!(!(0.0..f32::NAN).contains(&0.5));
    assert!(!(f32::NAN..1.0).contains(&0.5));
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    Returns `true` if the range contains no items.
    One-sided ranges (`RangeFrom`, etc) always return `false`.
    
    Examples
}
