// Extracted from library/alloc/src/boxed.rs:350
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        
        let mut five = Box::<u32>::try_new_uninit()?;
        // Deferred initialization:
        five.write(5);
        let five = unsafe { five.assume_init() };
        
        assert_eq!(*five, 5);
        Ok::<(), std::alloc::AllocError>(())
    }
    doctest().unwrap();
}
