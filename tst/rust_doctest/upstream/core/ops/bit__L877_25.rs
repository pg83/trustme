// Extracted from library/core/src/ops/bit.rs:877
#![allow(unused)]
fn main() {
    use std::ops::ShlAssign;

    #[derive(Debug, PartialEq)]
    struct Scalar(usize);

    impl ShlAssign<usize> for Scalar {
        fn shl_assign(&mut self, rhs: usize) {
            self.0 <<= rhs;
        }
    }

    let mut scalar = Scalar(4);
    scalar <<= 2;
    assert_eq!(scalar, Scalar(16));
}
