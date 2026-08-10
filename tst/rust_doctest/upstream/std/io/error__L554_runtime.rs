// Extracted from library/std/src/io/error.rs:554
#![allow(unused)]
fn main() {
    use std::io::{Error, ErrorKind};

    // errors can be created from strings
    let custom_error = Error::new(ErrorKind::Other, "oh no!");

    // errors can also be created from other errors
    let custom_error2 = Error::new(ErrorKind::Interrupted, custom_error);

    // creating an error without payload (and without memory allocation)
    let eof_error = Error::from(ErrorKind::UnexpectedEof);
}
