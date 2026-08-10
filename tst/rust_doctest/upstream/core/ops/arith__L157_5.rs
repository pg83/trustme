// Extracted from library/core/src/ops/arith.rs:157
#![allow(unused)]
fn main() {
    use std::ops::Sub;

    #[derive(Debug, PartialEq)]
    struct Point<T> {
        x: T,
        y: T,
    }

    // Notice that the implementation uses the associated type `Output`.
    impl<T: Sub<Output = T>> Sub for Point<T> {
        type Output = Self;

        fn sub(self, other: Self) -> Self::Output {
            Point {
                x: self.x - other.x,
                y: self.y - other.y,
            }
        }
    }

    assert_eq!(Point { x: 2, y: 3 } - Point { x: 1, y: 0 },
               Point { x: 1, y: 3 });
}
