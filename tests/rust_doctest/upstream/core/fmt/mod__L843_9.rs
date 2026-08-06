// Extracted from library/core/src/fmt/mod.rs:843
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        #[derive(Debug)]
        struct Point {
            x: i32,
            y: i32,
        }
        
        let origin = Point { x: 0, y: 0 };
        
        let expected = "The origin is: Point {
            x: 0,
            y: 0,
        }";
        assert_eq!(format!("The origin is: {origin:#?}"), expected);
        Ok(())
    }
    doctest().unwrap();
}
