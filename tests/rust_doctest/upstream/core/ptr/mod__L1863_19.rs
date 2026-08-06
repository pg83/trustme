// Extracted from library/core/src/ptr/mod.rs:1863
#![allow(unused)]
fn main() {
    let mut x = 0;
    let y = &mut x as *mut i32;
    let z = 12;
    
    unsafe {
        std::ptr::write(y, z);
        assert_eq!(std::ptr::read(y), 12);
    }
}
