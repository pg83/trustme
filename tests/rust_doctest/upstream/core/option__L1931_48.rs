// Extracted from library/core/src/option.rs:1931
#![allow(unused)]
#![feature(option_zip)]
fn main() {
    
    #[derive(Debug, PartialEq)]
    struct Point {
        x: f64,
        y: f64,
    }
    
    impl Point {
        fn new(x: f64, y: f64) -> Self {
            Self { x, y }
        }
    }
    
    let x = Some(17.5);
    let y = Some(42.7);
    
    assert_eq!(x.zip_with(y, Point::new), Some(Point { x: 17.5, y: 42.7 }));
    assert_eq!(x.zip_with(None, Point::new), None);
}
