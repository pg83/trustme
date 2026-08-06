// Extracted from library/alloc/src/ffi/c_str.rs:1181
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::borrow::Cow;
    
    assert_eq!(c"Hello World".to_string_lossy(), Cow::Borrowed("Hello World"));
}
