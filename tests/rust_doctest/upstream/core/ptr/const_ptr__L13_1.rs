// Extracted from library/core/src/ptr/const_ptr.rs:13
#![allow(unused)]
fn main() {
    let s: &str = "Follow the rabbit";
    let ptr: *const u8 = s.as_ptr();
    assert!(!ptr.is_null());
}
