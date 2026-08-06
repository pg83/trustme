// Extracted from library/core/src/num/nonzero.rs:107
#![allow(unused)]
fn main() {
    use std::num::NonZero;
    
    assert_eq!(size_of::<NonZero<u32>>(), size_of::<Option<NonZero<u32>>>());
    assert_eq!(align_of::<NonZero<u32>>(), align_of::<Option<NonZero<u32>>>());
}
