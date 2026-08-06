// Extracted from library/core/src/fmt/mod.rs:824
#![allow(unused)]
fn main() {
    use std::fmt;
    struct Point {
        x: i32,
        y: i32,
    }
    
    impl fmt::Debug for Point {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            write!(f, "Point [{} {}]", self.x, self.y)
        }
    }
}
