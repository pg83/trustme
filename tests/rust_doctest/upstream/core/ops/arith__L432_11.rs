// Extracted from library/core/src/ops/arith.rs:432
#![allow(unused)]
fn main() {
    use std::ops::Div;

    struct Scalar { value: f32 }

    #[derive(Debug, PartialEq)]
    struct Vector { value: Vec<f32> }

    impl Div<Scalar> for Vector {
        type Output = Self;

        fn div(self, rhs: Scalar) -> Self::Output {
            Self { value: self.value.iter().map(|v| v / rhs.value).collect() }
        }
    }

    let scalar = Scalar { value: 2f32 };
    let vector = Vector { value: vec![2f32, 4f32, 6f32] };
    assert_eq!(vector / scalar, Vector { value: vec![1f32, 2f32, 3f32] });
}
