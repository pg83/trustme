// Extracted from library/core/src/error.rs:479
#![feature(error_generic_member_access)]
use core::fmt;
use core::error::Request;
use core::error::request_ref;

#[derive(Debug)]
enum MyLittleTeaPot {
    Empty,
}

#[derive(Debug)]
struct MyBacktrace {
    // ...
}

impl MyBacktrace {
    fn new() -> MyBacktrace {
        // ...
        MyBacktrace {}
    }
}

#[derive(Debug)]
struct Error {
    backtrace: MyBacktrace,
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Example Error")
    }
}

impl std::error::Error for Error {
    fn provide<'a>(&'a self, request: &mut Request<'a>) {
        request
            .provide_ref::<MyBacktrace>(&self.backtrace);
    }
}

fn main() {
    let backtrace = MyBacktrace::new();
    let error = Error { backtrace };
    let dyn_error = &error as &dyn std::error::Error;
    let backtrace_ref = request_ref::<MyBacktrace>(dyn_error).unwrap();

    assert!(core::ptr::eq(&error.backtrace, backtrace_ref));
    assert!(request_ref::<MyLittleTeaPot>(dyn_error).is_none());
}
