// Extracted from library/std/src/io/error.rs:527
#![allow(unused)]
fn main() {
    use std::io::{Error, ErrorKind};
    
    let not_found = ErrorKind::NotFound;
    let error = Error::from(not_found);
    assert_eq!("entity not found", format!("{error}"));
}
