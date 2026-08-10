// Extracted from src/types/array.md:18
#![allow(unused)]
fn main() {
    // A stack-allocated array
    let array: [i32; 3] = [1, 2, 3];
    
    // A heap-allocated array, coerced to a slice
    let boxed_array: Box<[i32]> = Box::new([1, 2, 3]);
}
