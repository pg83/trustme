// Extracted from library/core/src/fmt/mod.rs:789
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fmt;
        
        struct Point {
            x: i32,
            y: i32,
        }
        
        impl fmt::Debug for Point {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.debug_struct("Point")
                 .field("x", &self.x)
                 .field("y", &self.y)
                 .finish()
            }
        }
        
        let origin = Point { x: 0, y: 0 };
        
        assert_eq!(
            format!("The origin is: {origin:?}"),
            "The origin is: Point { x: 0, y: 0 }",
        );
        Ok(())
    }
    doctest().unwrap();
}
