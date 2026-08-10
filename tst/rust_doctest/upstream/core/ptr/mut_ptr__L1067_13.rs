// Extracted from library/core/src/ptr/mut_ptr.rs:1067
#![allow(unused)]
fn main() {
    let s: &str = "123";

    unsafe {
        let end: *const u8 = s.as_ptr().add(3);
        assert_eq!('3', *end.sub(1) as char);
        assert_eq!('2', *end.sub(2) as char);
    }
}
