// Extracted from library/alloc/src/fmt.rs:125
#![allow(unused)]
extern crate alloc;
fn main() {
    // All of these print "Hello x    !"
    println!("Hello {:5}!", "x");
    println!("Hello {:1$}!", "x", 5);
    println!("Hello {1:0$}!", 5, "x");
    println!("Hello {:width$}!", "x", width = 5);
    let width = 5;
    println!("Hello {:width$}!", "x");
}
