// Extracted from library/core/src/slice/iter.rs:49
#![allow(unused)]
fn main() {
    // First, we need a slice to call the `iter` method on:
    let slice = &[1, 2, 3];
    
    // Then we call `iter` on the slice to get the `Iter` iterator,
    // and iterate over it:
    for element in slice.iter() {
        println!("{element}");
    }
    
    // This for loop actually already works without calling `iter`:
    for element in slice {
        println!("{element}");
    }
}
