// Extracted from src/lifetimes.md:236
#![allow(unused)]
fn main() {
    let mut data = vec![1, 2, 3];
    let x = &data[0];
    println!("{}", x);
    // This is OK, x is no longer needed
    data.push(4);
}
