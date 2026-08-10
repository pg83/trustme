// Extracted from library/alloc/src/ffi/c_str.rs:590
#![allow(unused)]
extern crate alloc;
fn main() {
    let c_string = c"foo".to_owned();
    let boxed = c_string.into_boxed_c_str();
    assert_eq!(boxed.to_bytes_with_nul(), b"foo\0");
}
