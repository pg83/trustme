// Extracted from library/core/src/ffi/c_str.rs:550
#![allow(unused)]
fn main() {
    assert_eq!(c"foo".to_bytes(), b"foo");
}
