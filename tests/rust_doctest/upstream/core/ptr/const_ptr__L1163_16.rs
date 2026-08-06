// Extracted from library/core/src/ptr/const_ptr.rs:1163
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fmt::Write;
        // Iterate using a raw pointer in increments of two elements (backwards)
        let data = [1u8, 2, 3, 4, 5];
        let mut ptr: *const u8 = data.as_ptr();
        let start_rounded_down = ptr.wrapping_sub(2);
        ptr = ptr.wrapping_add(4);
        let step = 2;
        let mut out = String::new();
        while ptr != start_rounded_down {
            unsafe {
                write!(&mut out, "{}, ", *ptr)?;
            }
            ptr = ptr.wrapping_sub(step);
        }
        assert_eq!(out, "5, 3, 1, ");
        std::fmt::Result::Ok(())
        Ok(())
    }
    doctest().unwrap();
}
