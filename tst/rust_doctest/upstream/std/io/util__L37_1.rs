// Extracted from library/std/src/io/util.rs:37
#![allow(unused)]
fn main() {
    use std::io::{self, Write};

    let buffer = vec![1, 2, 3, 5, 8];
    let num_bytes = io::empty().write(&buffer).unwrap();
    assert_eq!(num_bytes, 5);
}
