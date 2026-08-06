// Extracted from library/std/src/io/error.rs:732
use std::io::{Error, ErrorKind};

fn print_error(err: &Error) {
    if let Some(inner_err) = err.get_ref() {
        println!("Inner error: {inner_err:?}");
    } else {
        println!("No inner error");
    }
}

fn main() {
    // Will print "No inner error".
    print_error(&Error::last_os_error());
    // Will print "Inner error: ...".
    print_error(&Error::new(ErrorKind::Other, "oh no!"));
}
