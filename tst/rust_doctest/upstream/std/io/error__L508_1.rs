// Extracted from library/std/src/io/error.rs:508
#![allow(unused)]
fn main() {
    use std::io::ErrorKind;
    assert_eq!("entity not found", ErrorKind::NotFound.to_string());
}
