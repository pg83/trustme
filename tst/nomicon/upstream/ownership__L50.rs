// Extracted from src/ownership.md:50
#![allow(unused)]
fn main() {
    let mut data = vec![1, 2, 3];
    // get an internal reference
    let x = &data[0];
    
    // OH NO! `push` causes the backing storage of `data` to be reallocated.
    // Dangling pointer! Use after free! Alas!
    // (this does not compile in Rust)
    data.push(4);
    
    println!("{}", x);
}
