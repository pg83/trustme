// Extracted from library/std/src/os/hermit/ffi.rs:21
#![allow(unused)]
fn main() {
    use std::ffi::OsStr;
    use std::os::hermit::ffi::OsStrExt;
    
    let bytes = b"foo";
    
    // OsStrExt::from_bytes
    let os_str = OsStr::from_bytes(bytes);
    assert_eq!(os_str.to_str(), Some("foo"));
    
    // OsStrExt::as_bytes
    let bytes = os_str.as_bytes();
    assert_eq!(bytes, b"foo");
}
