// Extracted from library/std/src/os/fortanix_sgx/ffi.rs:6
#![allow(unused)]
fn main() {
    use std::ffi::OsString;
    use std::os::fortanix_sgx::ffi::OsStringExt;
    
    let bytes = b"foo".to_vec();
    
    // OsStringExt::from_vec
    let os_string = OsString::from_vec(bytes);
    assert_eq!(os_string.to_str(), Some("foo"));
    
    // OsStringExt::into_vec
    let bytes = os_string.into_vec();
    assert_eq!(bytes, b"foo");
}
