// Extracted from library/core/src/ptr/non_null.rs:508
#![allow(unused)]
#![feature(pointer_try_cast_aligned)]
fn main() {
    use std::ptr::NonNull;
    
    let mut x = 0u64;
    
    let aligned = NonNull::from_mut(&mut x);
    let unaligned = unsafe { aligned.byte_add(1) };
    
    assert!(aligned.try_cast_aligned::<u32>().is_some());
    assert!(unaligned.try_cast_aligned::<u32>().is_none());
}
