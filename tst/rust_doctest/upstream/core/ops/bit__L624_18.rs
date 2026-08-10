// Extracted from library/core/src/ops/bit.rs:624
#![allow(unused)]
fn main() {
    use std::ops::BitAndAssign;

    #[derive(Debug, PartialEq)]
    struct Scalar(bool);

    impl BitAndAssign for Scalar {
        // rhs is the "right-hand side" of the expression `a &= b`
        fn bitand_assign(&mut self, rhs: Self) {
            *self = Self(self.0 & rhs.0)
        }
    }

    let mut scalar = Scalar(true);
    scalar &= Scalar(true);
    assert_eq!(scalar, Scalar(true));

    let mut scalar = Scalar(true);
    scalar &= Scalar(false);
    assert_eq!(scalar, Scalar(false));

    let mut scalar = Scalar(false);
    scalar &= Scalar(true);
    assert_eq!(scalar, Scalar(false));

    let mut scalar = Scalar(false);
    scalar &= Scalar(false);
    assert_eq!(scalar, Scalar(false));
}
