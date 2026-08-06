// Extracted from library/core/src/ffi/c_str.rs:42
#![allow(unused)]
fn main() {
    use std::ffi::CStr;
    use std::os::raw::c_char;
    
    /* Extern functions are awkward in doc comments - fake it instead
    extern "C" { fn my_string() -> *const c_char; }
    */ unsafe extern "C" fn my_string() -> *const c_char { c"hello".as_ptr() }
    
    unsafe {
        let slice = CStr::from_ptr(my_string());
        println!("string buffer size without nul terminator: {}", slice.to_bytes().len());
    }
}
