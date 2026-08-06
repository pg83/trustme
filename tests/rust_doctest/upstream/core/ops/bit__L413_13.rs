// Extracted from library/core/src/ops/bit.rs:413
#![allow(unused)]
fn main() {
    use std::ops::Shl;
    
    #[derive(PartialEq, Debug)]
    struct SpinVector<T: Clone> {
        vec: Vec<T>,
    }
    
    impl<T: Clone> Shl<usize> for SpinVector<T> {
        type Output = Self;
    
        fn shl(self, rhs: usize) -> Self::Output {
            // Rotate the vector by `rhs` places.
            let (a, b) = self.vec.split_at(rhs);
            let mut spun_vector = vec![];
            spun_vector.extend_from_slice(b);
            spun_vector.extend_from_slice(a);
            Self { vec: spun_vector }
        }
    }
    
    assert_eq!(SpinVector { vec: vec![0, 1, 2, 3, 4] } << 2,
               SpinVector { vec: vec![2, 3, 4, 0, 1] });
}
