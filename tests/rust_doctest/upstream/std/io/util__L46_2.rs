// Extracted from library/std/src/io/util.rs:46
#![allow(unused)]
fn main() {
    use std::io::{self, Read};
    
    let mut buffer = String::new();
    io::empty().read_to_string(&mut buffer).unwrap();
    assert!(buffer.is_empty());
}
