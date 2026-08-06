// Extracted from library/core/src/primitive_docs.rs:1119
#![allow(unused)]
fn main() {
    fn calculate_point() -> (i32, i32) {
        // Don't do a calculation, that's not the point of the example
        (4, 5)
    }
    
    let point = calculate_point();
    
    assert_eq!(point.0, 4);
    assert_eq!(point.1, 5);
    
    // Combining this with patterns can be nicer.
    
    let (x, y) = calculate_point();
    
    assert_eq!(x, 4);
    assert_eq!(y, 5);
}
