// Extracted from library/core/src/ops/bit.rs:393
#![allow(unused)]
fn main() {
    use std::ops::Shl;

    #[derive(PartialEq, Debug)]
    struct Scalar(usize);

    impl Shl<Scalar> for Scalar {
        type Output = Self;

        fn shl(self, Self(rhs): Self) -> Self::Output {
            let Self(lhs) = self;
            Self(lhs << rhs)
        }
    }

    assert_eq!(Scalar(4) << Scalar(2), Scalar(16));
}
