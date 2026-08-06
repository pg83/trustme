// Extracted from library/std/src/keyword_docs.rs:1674
#![allow(unused)]
fn main() {
    trait Zero {
        const ZERO: Self;
        fn is_zero(&self) -> bool;
    }
    
    impl Zero for i32 {
        const ZERO: Self = 0;
    
        fn is_zero(&self) -> bool {
            *self == Self::ZERO
        }
    }
    
    assert_eq!(i32::ZERO, 0);
    assert!(i32::ZERO.is_zero());
    assert!(!4.is_zero());
}
