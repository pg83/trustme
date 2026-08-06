// Extracted from library/core/src/ops/arith.rs:534
#![allow(unused)]
fn main() {
    use std::ops::Rem;
    
    #[derive(PartialEq, Debug)]
    struct SplitSlice<'a, T> {
        slice: &'a [T],
    }
    
    impl<'a, T> Rem<usize> for SplitSlice<'a, T> {
        type Output = Self;
    
        fn rem(self, modulus: usize) -> Self::Output {
            let len = self.slice.len();
            let rem = len % modulus;
            let start = len - rem;
            Self {slice: &self.slice[start..]}
        }
    }
    
    // If we were to divide &[0, 1, 2, 3, 4, 5, 6, 7] into slices of size 3,
    // the remainder would be &[6, 7].
    assert_eq!(SplitSlice { slice: &[0, 1, 2, 3, 4, 5, 6, 7] } % 3,
               SplitSlice { slice: &[6, 7] });
}
