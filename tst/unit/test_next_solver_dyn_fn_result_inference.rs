//@ compile-pass

use std::ffi::CStr;
use std::io;

fn run<T>(f: &dyn Fn(&CStr) -> io::Result<T>) -> io::Result<T> {
    let value = CStr::from_bytes_with_nul(b"x\0").unwrap();
    f(value)
}

fn call() -> io::Result<()> {
    let ptr = run(&|p| Ok(p.as_ptr()))?;
    if ptr.is_null() {
        Err(io::Error::other("null"))
    } else {
        Ok(())
    }
}

fn main() {}
