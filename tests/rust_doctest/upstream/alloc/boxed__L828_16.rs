// Extracted from library/alloc/src/boxed.rs:828
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        
        use std::alloc::System;
        
        let mut values = Box::<[u32], _>::try_new_uninit_slice_in(3, System)?;
        // Deferred initialization:
        values[0].write(1);
        values[1].write(2);
        values[2].write(3);
        let values = unsafe { values.assume_init() };
        
        assert_eq!(*values, [1, 2, 3]);
        Ok::<(), std::alloc::AllocError>(())
    }
    doctest().unwrap();
}
