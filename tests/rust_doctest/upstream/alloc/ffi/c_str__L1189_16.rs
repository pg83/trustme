// Extracted from library/alloc/src/ffi/c_str.rs:1189
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::borrow::Cow;
    
    assert_eq!(
        c"Hello \xF0\x90\x80World".to_string_lossy(),
        Cow::Owned(String::from("Hello �World")) as Cow<'_, str>
    );
}
