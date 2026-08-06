// Extracted from library/core/src/ffi/c_str.rs:226
#![allow(unused)]
fn main() {
    use std::ffi::{c_char, CStr};
    
    fn my_string() -> *const c_char {
        c"hello".as_ptr()
    }
    
    unsafe {
        let slice = CStr::from_ptr(my_string());
        assert_eq!(slice.to_str().unwrap(), "hello");
    }
}
