// Extracted from library/core/src/ptr/mut_ptr.rs:1177
#![allow(unused)]
fn main() {
    // Iterate using a raw pointer in increments of two elements
    let data = [1u8, 2, 3, 4, 5];
    let mut ptr: *const u8 = data.as_ptr();
    let step = 2;
    let end_rounded_up = ptr.wrapping_add(6);
    
    // This loop prints "1, 3, 5, "
    while ptr != end_rounded_up {
        unsafe {
            print!("{}, ", *ptr);
        }
        ptr = ptr.wrapping_add(step);
    }
}
