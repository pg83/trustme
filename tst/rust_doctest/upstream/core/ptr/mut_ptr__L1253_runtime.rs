// Extracted from library/core/src/ptr/mut_ptr.rs:1253
#![allow(unused)]
fn main() {
    // Iterate using a raw pointer in increments of two elements (backwards)
    let data = [1u8, 2, 3, 4, 5];
    let mut ptr: *const u8 = data.as_ptr();
    let start_rounded_down = ptr.wrapping_sub(2);
    ptr = ptr.wrapping_add(4);
    let step = 2;
    // This loop prints "5, 3, 1, "
    while ptr != start_rounded_down {
        unsafe {
            print!("{}, ", *ptr);
        }
        ptr = ptr.wrapping_sub(step);
    }
}
