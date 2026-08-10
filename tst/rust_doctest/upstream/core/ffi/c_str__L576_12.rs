// Extracted from library/core/src/ffi/c_str.rs:576
#![allow(unused)]
fn main() {
    assert_eq!(c"foo".to_bytes_with_nul(), b"foo\0");
}
