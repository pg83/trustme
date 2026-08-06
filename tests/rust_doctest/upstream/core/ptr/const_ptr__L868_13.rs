// Extracted from library/core/src/ptr/const_ptr.rs:868
#![allow(unused)]
fn main() {
    let s: &str = "123";
    let ptr: *const u8 = s.as_ptr();
    
    unsafe {
        assert_eq!(*ptr.add(1), b'2');
        assert_eq!(*ptr.add(2), b'3');
    }
}
