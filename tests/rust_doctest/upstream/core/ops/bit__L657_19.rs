// Extracted from library/core/src/ops/bit.rs:657
#![allow(unused)]
fn main() {
    use std::ops::BitAndAssign;

    #[derive(Debug, PartialEq)]
    struct BooleanVector(Vec<bool>);

    impl BitAndAssign for BooleanVector {
        // `rhs` is the "right-hand side" of the expression `a &= b`.
        fn bitand_assign(&mut self, rhs: Self) {
            assert_eq!(self.0.len(), rhs.0.len());
            *self = Self(
                self.0
                    .iter()
                    .zip(rhs.0.iter())
                    .map(|(x, y)| *x & *y)
                    .collect()
            );
        }
    }

    let mut bv = BooleanVector(vec![true, true, false, false]);
    bv &= BooleanVector(vec![true, false, true, false]);
    let expected = BooleanVector(vec![true, false, false, false]);
    assert_eq!(bv, expected);
}
