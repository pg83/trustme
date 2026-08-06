// Extracted from library/alloc/src/boxed.rs:429
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        
        use std::alloc::System;
        
        let five = Box::try_new_in(5, System)?;
        Ok::<(), std::alloc::AllocError>(())
    }
    doctest().unwrap();
}
