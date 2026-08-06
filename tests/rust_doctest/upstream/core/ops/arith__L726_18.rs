// Extracted from library/core/src/ops/arith.rs:726
#![allow(unused)]
fn main() {
    use std::ops::AddAssign;
    
    #[derive(Debug, Copy, Clone, PartialEq)]
    struct Point {
        x: i32,
        y: i32,
    }
    
    impl AddAssign for Point {
        fn add_assign(&mut self, other: Self) {
            *self = Self {
                x: self.x + other.x,
                y: self.y + other.y,
            };
        }
    }
    
    let mut point = Point { x: 1, y: 0 };
    point += Point { x: 2, y: 3 };
    assert_eq!(point, Point { x: 3, y: 3 });
}
