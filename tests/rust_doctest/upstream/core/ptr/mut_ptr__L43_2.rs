// Extracted from library/core/src/ptr/mut_ptr.rs:43
#![allow(unused)]
#![feature(pointer_try_cast_aligned)]
fn main() {
    
    let mut x = 0u64;
    
    let aligned: *mut u64 = &mut x;
    let unaligned = unsafe { aligned.byte_add(1) };
    
    assert!(aligned.try_cast_aligned::<u32>().is_some());
    assert!(unaligned.try_cast_aligned::<u32>().is_none());
}
