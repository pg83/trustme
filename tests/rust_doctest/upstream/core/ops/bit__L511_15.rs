// Extracted from library/core/src/ops/bit.rs:511
#![allow(unused)]
fn main() {
    use std::ops::Shr;

    #[derive(PartialEq, Debug)]
    struct Scalar(usize);

    impl Shr<Scalar> for Scalar {
        type Output = Self;

        fn shr(self, Self(rhs): Self) -> Self::Output {
            let Self(lhs) = self;
            Self(lhs >> rhs)
        }
    }

    assert_eq!(Scalar(16) >> Scalar(2), Scalar(4));
}
