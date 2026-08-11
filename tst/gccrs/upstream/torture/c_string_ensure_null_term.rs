use std::ffi::CStr;
fn gccrs_main() -> i32 {
    let value: &CStr = c"gccrs";
    let terminator = unsafe { *value.as_ptr().add(5) };
    terminator as i32
}
fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
