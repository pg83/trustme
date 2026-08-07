// Extracted from library/core/src/ptr/mut_ptr.rs:494
#![allow(unused)]
fn main() {
    // Iterate using a raw pointer in increments of two elements
    let mut data = [1u8, 2, 3, 4, 5];
    let mut ptr: *mut u8 = data.as_mut_ptr();
    let step = 2;
    let end_rounded_up = ptr.wrapping_offset(6);

    while ptr != end_rounded_up {
        unsafe {
            *ptr = 0;
        }
        ptr = ptr.wrapping_offset(step);
    }
    assert_eq!(&data, &[0, 2, 0, 4, 0]);
}
