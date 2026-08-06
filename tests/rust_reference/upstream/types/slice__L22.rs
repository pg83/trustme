// Extracted from src/types/slice.md:22
#![allow(unused)]
fn main() {
    // A heap-allocated array, coerced to a slice
    let boxed_array: Box<[i32]> = Box::new([1, 2, 3]);
    
    // A (shared) slice into an array
    let slice: &[i32] = &boxed_array[..];
}
