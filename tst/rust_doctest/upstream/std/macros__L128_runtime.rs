// Extracted from library/std/src/macros.rs:128
#![allow(unused)]
fn main() {
    println!(); // prints just a newline
    println!("hello there!");
    println!("format {} arguments", "some");
    let local_variable = "some";
    println!("format {local_variable} arguments");
}
