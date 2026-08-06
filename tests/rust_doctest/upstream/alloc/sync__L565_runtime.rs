// Extracted from library/alloc/src/sync.rs:565
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::sync::Arc;
        
        let five = Arc::try_new(5)?;
        Ok::<(), std::alloc::AllocError>(())
    }
    doctest().unwrap();
}
