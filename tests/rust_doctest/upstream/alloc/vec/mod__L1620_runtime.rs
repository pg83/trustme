// Extracted from library/alloc/src/vec/mod.rs:1620
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::io::{self, Write};
    let buffer = vec![1, 2, 3, 5, 8];
    io::sink().write(buffer.as_slice()).unwrap();
}
