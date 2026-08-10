// Extracted from library/core/src/ptr/mod.rs:2091
#![allow(unused)]
fn main() {
    let x = 12;
    let y = &x as *const i32;

    unsafe {
        assert_eq!(std::ptr::read_volatile(y), 12);
    }
}
