// Extracted from library/alloc/src/rc.rs:901
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        
        use std::rc::Rc;
        use std::alloc::System;
        
        let zero = Rc::<u32, _>::try_new_zeroed_in(System)?;
        let zero = unsafe { zero.assume_init() };
        
        assert_eq!(*zero, 0);
        Ok::<(), std::alloc::AllocError>(())
    }
    doctest().unwrap();
}
