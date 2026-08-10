// Extracted from library/std/src/io/error.rs:978
use std::io::{Error, ErrorKind};

fn print_error(err: Error) {
    println!("{:?}", err.kind());
}

fn main() {
    // As no error has (visibly) occurred, this may print anything!
    // It likely prints a placeholder for unidentified (non-)errors.
    print_error(Error::last_os_error());
    // Will print "AddrInUse".
    print_error(Error::new(ErrorKind::AddrInUse, "oh no!"));
}
