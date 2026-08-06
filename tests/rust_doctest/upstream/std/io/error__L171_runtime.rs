// Extracted from library/std/src/io/error.rs:171
#![allow(unused)]
#![feature(io_const_error)]
fn main() {
    use std::io::{const_error, Error, ErrorKind};
    
    const FAIL: Error = const_error!(ErrorKind::Unsupported, "tried something that never works");
    
    fn not_here() -> Result<(), Error> {
        Err(FAIL)
    }
}
