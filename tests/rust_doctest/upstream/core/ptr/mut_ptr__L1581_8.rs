// Extracted from library/core/src/ptr/mut_ptr.rs:1581
#![allow(unused)]
fn main() {
    unsafe {
    let mut x = [5_u8, 6, 7, 8, 9];
    let ptr = x.as_mut_ptr();
    let offset = ptr.align_offset(align_of::<u16>());
    
    if offset < x.len() - 1 {
        let u16_ptr = ptr.add(offset).cast::<u16>();
        *u16_ptr = 0;
    
        assert!(x == [0, 0, 7, 8, 9] || x == [5, 0, 0, 8, 9]);
    } else {
        // while the pointer can be aligned via `offset`, it would point
        // outside the allocation
    }
    }
}
