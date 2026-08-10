// Extracted from library/alloc/src/fmt.rs:117
#![allow(unused)]
extern crate alloc;
fn main() {
    let a = 5;
    let b = &a;
    println!("{a:e} {b:p}"); // => 5e0 0x7ffe37b7273c
}
