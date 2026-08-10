// Extracted from library/core/src/ptr/const_ptr.rs:1084
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fmt::Write;
        // Iterate using a raw pointer in increments of two elements
        let data = [1u8, 2, 3, 4, 5];
        let mut ptr: *const u8 = data.as_ptr();
        let step = 2;
        let end_rounded_up = ptr.wrapping_add(6);

        let mut out = String::new();
        while ptr != end_rounded_up {
            unsafe {
                write!(&mut out, "{}, ", *ptr)?;
            }
            ptr = ptr.wrapping_add(step);
        }
        assert_eq!(out, "1, 3, 5, ");
        std::fmt::Result::Ok(())
    }
    doctest().unwrap();
}
