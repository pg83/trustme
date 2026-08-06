// Extracted from library/alloc/src/sync.rs:955
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        
        use std::sync::Arc;
        use std::alloc::System;
        
        let zero = Arc::<u32, _>::try_new_zeroed_in(System)?;
        let zero = unsafe { zero.assume_init() };
        
        assert_eq!(*zero, 0);
        Ok::<(), std::alloc::AllocError>(())
    }
    doctest().unwrap();
}
