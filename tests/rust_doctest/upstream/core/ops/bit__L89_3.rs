// Extracted from library/core/src/ops/bit.rs:89
#![allow(unused)]
fn main() {
    use std::ops::BitAnd;
    
    #[derive(Debug, PartialEq)]
    struct Scalar(bool);
    
    impl BitAnd for Scalar {
        type Output = Self;
    
        // rhs is the "right-hand side" of the expression `a & b`
        fn bitand(self, rhs: Self) -> Self::Output {
            Self(self.0 & rhs.0)
        }
    }
    
    assert_eq!(Scalar(true) & Scalar(true), Scalar(true));
    assert_eq!(Scalar(true) & Scalar(false), Scalar(false));
    assert_eq!(Scalar(false) & Scalar(true), Scalar(false));
    assert_eq!(Scalar(false) & Scalar(false), Scalar(false));
}
