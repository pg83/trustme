// Extracted from library/alloc/src/ffi/c_str.rs:431
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::ffi::CString;

    let c_string = CString::from(c"foo");

    let ptr = c_string.into_raw();

    unsafe {
        assert_eq!(b'f', *ptr as u8);
        assert_eq!(b'o', *ptr.add(1) as u8);
        assert_eq!(b'o', *ptr.add(2) as u8);
        assert_eq!(b'\0', *ptr.add(3) as u8);

        // retake pointer to free memory
        let _ = CString::from_raw(ptr);
    }
}
