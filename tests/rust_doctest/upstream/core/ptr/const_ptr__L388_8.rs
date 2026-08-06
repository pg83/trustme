// Extracted from library/core/src/ptr/const_ptr.rs:388
#![allow(unused)]
fn main() {
    let s: &str = "123";
    let ptr: *const u8 = s.as_ptr();
    
    unsafe {
        assert_eq!(*ptr.offset(1) as char, '2');
        assert_eq!(*ptr.offset(2) as char, '3');
    }
}
