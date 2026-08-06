// Extracted from library/alloc/src/ffi/c_str.rs:1209
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::ffi::{CStr, CString};
    
    let boxed: Box<CStr> = Box::from(c"foo");
    let c_string: CString = c"foo".to_owned();
    
    assert_eq!(boxed.into_c_string(), c_string);
}
