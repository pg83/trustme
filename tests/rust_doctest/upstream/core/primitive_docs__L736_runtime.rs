// Extracted from library/core/src/primitive_docs.rs:736
#![allow(unused)]
#![allow(array_into_iter)] // override our `deny(warnings)`
fn main() {
    // Rust 2015 and 2018:
    
    let array: [i32; 3] = [0; 3];
    
    // This creates a slice iterator, producing references to each value.
    for item in array.into_iter().enumerate() {
        let (i, x): (usize, &i32) = item;
        println!("array[{i}] = {x}");
    }
    
    // The `array_into_iter` lint suggests this change for future compatibility:
    for item in array.iter().enumerate() {
        let (i, x): (usize, &i32) = item;
        println!("array[{i}] = {x}");
    }
    
    // You can explicitly iterate an array by value using `IntoIterator::into_iter`
    for item in IntoIterator::into_iter(array).enumerate() {
        let (i, x): (usize, i32) = item;
        println!("array[{i}] = {x}");
    }
}
