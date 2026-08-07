// Extracted from library/core/src/ffi/c_str.rs:597
#![allow(unused)]
#![feature(cstr_bytes)]
fn main() {

    assert!(c"foo".bytes().eq(*b"foo"));
}
