// Extracted from library/alloc/src/vec/mod.rs:1652
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::io::{self, Read};
    let mut buffer = vec![0; 3];
    io::repeat(0b101).read_exact(buffer.as_mut_slice()).unwrap();
}
