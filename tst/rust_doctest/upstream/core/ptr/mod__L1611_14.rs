// Extracted from library/core/src/ptr/mod.rs:1611
#![allow(unused)]
fn main() {
    let x = 12;
    let y = &x as *const i32;

    unsafe {
        assert_eq!(std::ptr::read(y), 12);
    }
}
