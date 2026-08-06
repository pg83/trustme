// Extracted from library/core/src/ops/bit.rs:959
#![allow(unused)]
fn main() {
    use std::ops::ShrAssign;
    
    #[derive(Debug, PartialEq)]
    struct Scalar(usize);
    
    impl ShrAssign<usize> for Scalar {
        fn shr_assign(&mut self, rhs: usize) {
            self.0 >>= rhs;
        }
    }
    
    let mut scalar = Scalar(16);
    scalar >>= 2;
    assert_eq!(scalar, Scalar(4));
}
