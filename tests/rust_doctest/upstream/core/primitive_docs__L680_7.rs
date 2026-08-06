// Extracted from library/core/src/primitive_docs.rs:680
#![allow(unused)]
fn main() {
    let mut array: [i32; 3] = [0; 3];
    
    array[1] = 1;
    array[2] = 2;
    
    assert_eq!([1, 2], &array[1..]);
    
    // This loop prints: 0 1 2
    for x in array {
        print!("{x} ");
    }
}
