// Extracted from library/alloc/src/boxed.rs:869
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        
        use std::alloc::System;
        
        let values = Box::<[u32], _>::try_new_zeroed_slice_in(3, System)?;
        let values = unsafe { values.assume_init() };
        
        assert_eq!(*values, [0, 0, 0]);
        Ok::<(), std::alloc::AllocError>(())
    }
    doctest().unwrap();
}
