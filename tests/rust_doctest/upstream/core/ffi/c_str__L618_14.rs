// Extracted from library/core/src/ffi/c_str.rs:618
#![allow(unused)]
fn main() {
    assert_eq!(c"foo".to_str(), Ok("foo"));
}
