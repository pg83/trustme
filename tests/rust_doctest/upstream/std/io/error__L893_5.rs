// Extracted from library/std/src/io/error.rs:893
use std::fmt;
use std::io;
use std::error::Error;

#[derive(Debug)]
enum E {
    Io(io::Error),
    SomeOtherVariant,
}

impl fmt::Display for E {
   // ...
   fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
       todo!()
   }
}
impl Error for E {}

impl From<io::Error> for E {
    fn from(err: io::Error) -> E {
        err.downcast::<E>()
            .unwrap_or_else(E::Io)
    }
}

impl From<E> for io::Error {
    fn from(err: E) -> io::Error {
        match err {
            E::Io(io_error) => io_error,
            e => io::Error::new(io::ErrorKind::Other, e),
        }
    }
}

fn main() {
let e = E::SomeOtherVariant;
// Convert it to an io::Error
let io_error = io::Error::from(e);
// Cast it back to the original variant
let e = E::from(io_error);
assert!(matches!(e, E::SomeOtherVariant));

let io_error = io::Error::from(io::ErrorKind::AlreadyExists);
// Convert it to E
let e = E::from(io_error);
// Cast it back to the original variant
let io_error = io::Error::from(e);
assert_eq!(io_error.kind(), io::ErrorKind::AlreadyExists);
assert!(io_error.get_ref().is_none());
assert!(io_error.raw_os_error().is_none());
}
