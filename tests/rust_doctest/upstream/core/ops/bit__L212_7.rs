// Extracted from library/core/src/ops/bit.rs:212
#![allow(unused)]
fn main() {
    use std::ops::BitOr;

    #[derive(Debug, PartialEq)]
    struct BooleanVector(Vec<bool>);

    impl BitOr for BooleanVector {
        type Output = Self;

        fn bitor(self, Self(rhs): Self) -> Self::Output {
            let Self(lhs) = self;
            assert_eq!(lhs.len(), rhs.len());
            Self(
                lhs.iter()
                    .zip(rhs.iter())
                    .map(|(x, y)| *x | *y)
                    .collect()
            )
        }
    }

    let bv1 = BooleanVector(vec![true, true, false, false]);
    let bv2 = BooleanVector(vec![true, false, true, false]);
    let expected = BooleanVector(vec![true, true, true, false]);
    assert_eq!(bv1 | bv2, expected);
}
