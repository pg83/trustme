// Extracted from library/std/src/io/util.rs:254
#![allow(unused)]
fn main() {
    use std::io::{self, Read};
    
    let mut buffer = [0; 3];
    io::repeat(0b101).read_exact(&mut buffer).unwrap();
    assert_eq!(buffer, [0b101, 0b101, 0b101]);
}
