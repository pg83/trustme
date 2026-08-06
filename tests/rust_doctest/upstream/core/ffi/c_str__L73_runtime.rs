// Extracted from library/core/src/ffi/c_str.rs:73
#![allow(unused)]
fn main() {
    use std::ffi::CStr;
    use std::os::raw::c_char;
    
    /* Extern functions are awkward in doc comments - fake it instead
    extern "C" { fn my_string() -> *const c_char; }
    */ unsafe extern "C" fn my_string() -> *const c_char { c"hello".as_ptr() }
    
    fn my_string_safe() -> String {
        let cstr = unsafe { CStr::from_ptr(my_string()) };
        // Get a copy-on-write Cow<'_, str>, then extract the
        // allocated String (or allocate a fresh one if needed).
        cstr.to_string_lossy().into_owned()
    }
    
    println!("string: {}", my_string_safe());
}
