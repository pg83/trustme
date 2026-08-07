// Extracted from library/core/src/ffi/c_str.rs:284
#![allow(unused)]
fn main() {
    use std::ffi::CStr;

    let mut buffer = [0u8; 16];
    unsafe {
        // Here we might call an unsafe C function that writes a string
        // into the buffer.
        let buf_ptr = buffer.as_mut_ptr();
        buf_ptr.write_bytes(b'A', 8);
    }
    // Attempt to extract a C nul-terminated string from the buffer.
    let c_str = CStr::from_bytes_until_nul(&buffer[..]).unwrap();
    assert_eq!(c_str.to_str().unwrap(), "AAAAAAAA");
}
