#![allow(unreachable_code)]
//@ compile-fail: error
fn escape(pointer: *const str) -> *const str {
    &(return pointer)
}

fn main() {
    let text: &str = "north";
    let pointer = text as *const str;
    assert!(std::ptr::eq(pointer, escape(pointer)));
}
