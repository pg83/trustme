// Extracted from library/core/src/ptr/mut_ptr.rs:961
#![allow(unused)]
fn main() {
    let mut s: String = "123".to_string();
    let ptr: *mut u8 = s.as_mut_ptr();

    unsafe {
        assert_eq!('2', *ptr.add(1) as char);
        assert_eq!('3', *ptr.add(2) as char);
    }
}
