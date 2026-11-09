//@ compile-fail: type annotations needed
use std::ops::{Add, Sub};

#[derive(Default)]
struct Left(i32);

#[derive(Default)]
struct Right(i32);

impl Add<Right> for i32 {
    type Output = Left;
    fn add(self, rhs: Right) -> Left {
        Left(self + rhs.0)
    }
}

impl Add<Left> for i64 {
    type Output = Right;
    fn add(self, rhs: Left) -> Right {
        Right(self as i32 + rhs.0)
    }
}

impl Sub<Left> for i32 {
    type Output = Right;
    fn sub(self, rhs: Left) -> Right {
        Right(self - rhs.0)
    }
}

impl Sub<Right> for i64 {
    type Output = Left;
    fn sub(self, rhs: Right) -> Left {
        Left(self as i32 - rhs.0)
    }
}

fn main() {
    let mut left = Default::default();
    let mut right = Default::default();
    left = 1 + right;
    right = 1 - left;
    assert_eq!(left.0, 1);
    assert_eq!(right.0, 0);
}
