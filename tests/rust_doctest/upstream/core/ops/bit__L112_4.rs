// Extracted from library/core/src/ops/bit.rs:112
#![allow(unused)]
fn main() {
    use std::ops::BitAnd;

    #[derive(Debug, PartialEq)]
    struct BooleanVector(Vec<bool>);

    impl BitAnd for BooleanVector {
        type Output = Self;

        fn bitand(self, Self(rhs): Self) -> Self::Output {
            let Self(lhs) = self;
            assert_eq!(lhs.len(), rhs.len());
            Self(
                lhs.iter()
                    .zip(rhs.iter())
                    .map(|(x, y)| *x & *y)
                    .collect()
            )
        }
    }

    let bv1 = BooleanVector(vec![true, true, false, false]);
    let bv2 = BooleanVector(vec![true, false, true, false]);
    let expected = BooleanVector(vec![true, false, false, false]);
    assert_eq!(bv1 & bv2, expected);
}
