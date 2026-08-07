// Extracted from library/alloc/src/boxed.rs:147
#![allow(unused)]
#![allow(boxed_slice_into_iter)] // override our `deny(warnings)`
extern crate alloc;
fn main() {
    // Rust 2015, 2018, and 2021:

    let boxed_slice: Box<[i32]> = vec![0; 3].into_boxed_slice();

    // This creates a slice iterator, producing references to each value.
    for item in boxed_slice.into_iter().enumerate() {
        let (i, x): (usize, &i32) = item;
        println!("boxed_slice[{i}] = {x}");
    }

    // The `boxed_slice_into_iter` lint suggests this change for future compatibility:
    for item in boxed_slice.iter().enumerate() {
        let (i, x): (usize, &i32) = item;
        println!("boxed_slice[{i}] = {x}");
    }

    // You can explicitly iterate a boxed slice by value using `IntoIterator::into_iter`
    for item in IntoIterator::into_iter(boxed_slice).enumerate() {
        let (i, x): (usize, i32) = item;
        println!("boxed_slice[{i}] = {x}");
    }
}
