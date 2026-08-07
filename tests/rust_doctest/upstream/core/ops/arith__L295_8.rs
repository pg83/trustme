// Extracted from library/core/src/ops/arith.rs:295
#![allow(unused)]
fn main() {
    use std::ops::Mul;

    struct Scalar { value: usize }

    #[derive(Debug, PartialEq)]
    struct Vector { value: Vec<usize> }

    impl Mul<Scalar> for Vector {
        type Output = Self;

        fn mul(self, rhs: Scalar) -> Self::Output {
            Self { value: self.value.iter().map(|v| v * rhs.value).collect() }
        }
    }

    let vector = Vector { value: vec![2, 4, 6] };
    let scalar = Scalar { value: 3 };
    assert_eq!(vector * scalar, Vector { value: vec![6, 12, 18] });
}
