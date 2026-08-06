// Extracted from library/core/src/ptr/const_ptr.rs:60
#![allow(unused)]
#![feature(pointer_try_cast_aligned)]
fn main() {
    
    let x = 0u64;
    
    let aligned: *const u64 = &x;
    let unaligned = unsafe { aligned.byte_add(1) };
    
    assert!(aligned.try_cast_aligned::<u32>().is_some());
    assert!(unaligned.try_cast_aligned::<u32>().is_none());
}
