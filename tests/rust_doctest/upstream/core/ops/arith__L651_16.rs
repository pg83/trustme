// Extracted from library/core/src/ops/arith.rs:651
#![allow(unused)]
fn main() {
    use std::ops::Neg;
    
    #[derive(Debug, PartialEq)]
    enum Sign {
        Negative,
        Zero,
        Positive,
    }
    
    impl Neg for Sign {
        type Output = Self;
    
        fn neg(self) -> Self::Output {
            match self {
                Sign::Negative => Sign::Positive,
                Sign::Zero => Sign::Zero,
                Sign::Positive => Sign::Negative,
            }
        }
    }
    
    // A negative positive is a negative.
    assert_eq!(-Sign::Positive, Sign::Negative);
    // A double negative is a positive.
    assert_eq!(-Sign::Negative, Sign::Positive);
    // Zero is its own negation.
    assert_eq!(-Sign::Zero, Sign::Zero);
}
