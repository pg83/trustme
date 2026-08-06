// Extracted from library/alloc/src/sync.rs:911
#![allow(unused)]
#![feature(allocator_api)]
#![feature(get_mut_unchecked)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        
        use std::sync::Arc;
        use std::alloc::System;
        
        let mut five = Arc::<u32, _>::try_new_uninit_in(System)?;
        
        let five = unsafe {
            // Deferred initialization:
            Arc::get_mut_unchecked(&mut five).as_mut_ptr().write(5);
        
            five.assume_init()
        };
        
        assert_eq!(*five, 5);
        Ok::<(), std::alloc::AllocError>(())
    }
    doctest().unwrap();
}
