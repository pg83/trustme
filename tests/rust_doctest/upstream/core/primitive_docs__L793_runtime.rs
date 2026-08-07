// Extracted from library/core/src/primitive_docs.rs:793
#![allow(unused)]
fn main() {
    // Rust 2015 and 2018:

    let array: [i32; 3] = [0; 3];

    // This iterates by reference:
    for item in array.iter() {
        let x: &i32 = item;
        println!("{x}");
    }

    // This iterates by value:
    for item in IntoIterator::into_iter(array) {
        let x: i32 = item;
        println!("{x}");
    }

    // This iterates by value:
    for item in array {
        let x: i32 = item;
        println!("{x}");
    }

    // IntoIter can also start a chain.
    // This iterates by value:
    for item in IntoIterator::into_iter(array).enumerate() {
        let (i, x): (usize, i32) = item;
        println!("array[{i}] = {x}");
    }
}
