// Extracted from library/alloc/src/boxed.rs:333
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        
        let five = Box::try_new(5)?;
        Ok::<(), std::alloc::AllocError>(())
    }
    doctest().unwrap();
}
