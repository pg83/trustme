// Extracted from library/core/src/ffi/c_str.rs:525
#![allow(unused)]
fn main() {
    assert!(!c"foo".is_empty());
    assert!(c"".is_empty());
}
