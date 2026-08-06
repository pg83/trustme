// Extracted from library/alloc/src/sync.rs:879
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        
        use std::sync::Arc;
        use std::alloc::System;
        
        let five = Arc::try_new_in(5, System)?;
        Ok::<(), std::alloc::AllocError>(())
    }
    doctest().unwrap();
}
