//@ compile-flags: -Zdump-ast
//@ output-contains: _1_ast.rs "text
//@ output-contains: _1_ast.rs b"bytes
//@ output-contains: _1_ast.rs c"cstring
use std::ffi::CStr;
const TEXT: &str = "text\nβ\"\\";
const BYTES: &[u8] = b"bytes\0\xff\"\\";
const C: &CStr = c"cstring\n\"\\";
fn main() {
    assert_eq!(TEXT, "text\nβ\"\\");
    assert_eq!(BYTES, b"bytes\0\xff\"\\");
    assert_eq!(C.to_bytes(), b"cstring\n\"\\");
}
