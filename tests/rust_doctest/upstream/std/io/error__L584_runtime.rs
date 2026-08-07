// Extracted from library/std/src/io/error.rs:584
#![allow(unused)]
fn main() {
    use std::io::Error;

    // errors can be created from strings
    let custom_error = Error::other("oh no!");

    // errors can also be created from other errors
    let custom_error2 = Error::other(custom_error);
}
