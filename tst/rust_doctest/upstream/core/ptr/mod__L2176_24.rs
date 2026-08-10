// Extracted from library/core/src/ptr/mod.rs:2176
#![allow(unused)]
fn main() {
    let mut x = 0;
    let y = &mut x as *mut i32;
    let z = 12;

    unsafe {
        std::ptr::write_volatile(y, z);
        assert_eq!(std::ptr::read_volatile(y), 12);
    }
}
