// Extracted from library/core/src/ops/mod.rs:43
#![allow(unused)]
fn main() {
    use std::ops::{Add, Sub};

    #[derive(Debug, Copy, Clone, PartialEq)]
    struct Point {
        x: i32,
        y: i32,
    }

    impl Add for Point {
        type Output = Self;

        fn add(self, other: Self) -> Self {
            Self {x: self.x + other.x, y: self.y + other.y}
        }
    }

    impl Sub for Point {
        type Output = Self;

        fn sub(self, other: Self) -> Self {
            Self {x: self.x - other.x, y: self.y - other.y}
        }
    }

    assert_eq!(Point {x: 3, y: 3}, Point {x: 1, y: 0} + Point {x: 2, y: 3});
    assert_eq!(Point {x: -1, y: -3}, Point {x: 1, y: 0} - Point {x: 2, y: 3});
}
