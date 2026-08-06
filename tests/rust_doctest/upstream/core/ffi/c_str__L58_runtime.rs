// Extracted from library/core/src/ffi/c_str.rs:58
#![allow(unused)]
fn main() {
    use std::ffi::CStr;
    use std::os::raw::c_char;
    
    fn work(data: &CStr) {
        unsafe extern "C" fn work_with(s: *const c_char) {}
        unsafe { work_with(data.as_ptr()) }
    }
    
    let s = c"Hello world!";
    work(&s);
}
