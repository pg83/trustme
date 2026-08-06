// Extracted from library/core/src/ptr/const_ptr.rs:974
#![allow(unused)]
fn main() {
    let s: &str = "123";
    
    unsafe {
        let end: *const u8 = s.as_ptr().add(3);
        assert_eq!(*end.sub(1), b'3');
        assert_eq!(*end.sub(2), b'2');
    }
}
