// Extracted from library/std/src/ffi/os_str.rs:1087
#![allow(unused)]
#![feature(os_str_slice)]
fn main() {

    use std::ffi::OsStr;

    let os_str = OsStr::new("foo=bar");
    let bytes = os_str.as_encoded_bytes();
    if let Some(index) = bytes.iter().position(|b| *b == b'=') {
        let key = os_str.slice_encoded_bytes(..index);
        let value = os_str.slice_encoded_bytes(index + 1..);
        assert_eq!(key, "foo");
        assert_eq!(value, "bar");
    }
}
