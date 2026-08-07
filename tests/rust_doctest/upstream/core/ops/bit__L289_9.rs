// Extracted from library/core/src/ops/bit.rs:289
#![allow(unused)]
fn main() {
    use std::ops::BitXor;

    #[derive(Debug, PartialEq)]
    struct Scalar(bool);

    impl BitXor for Scalar {
        type Output = Self;

        // rhs is the "right-hand side" of the expression `a ^ b`
        fn bitxor(self, rhs: Self) -> Self::Output {
            Self(self.0 ^ rhs.0)
        }
    }

    assert_eq!(Scalar(true) ^ Scalar(true), Scalar(false));
    assert_eq!(Scalar(true) ^ Scalar(false), Scalar(true));
    assert_eq!(Scalar(false) ^ Scalar(true), Scalar(true));
    assert_eq!(Scalar(false) ^ Scalar(false), Scalar(false));
}
