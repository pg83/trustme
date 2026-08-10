// Extracted from library/std/src/io/error.rs:693
use std::io::{Error, ErrorKind};

fn print_os_error(err: &Error) {
    if let Some(raw_os_err) = err.raw_os_error() {
        println!("raw OS error: {raw_os_err:?}");
    } else {
        println!("Not an OS error");
    }
}

fn main() {
    // Will print "raw OS error: ...".
    print_os_error(&Error::last_os_error());
    // Will print "Not an OS error".
    print_os_error(&Error::new(ErrorKind::Other, "oh no!"));
}
