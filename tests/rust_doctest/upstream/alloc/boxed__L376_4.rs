// Extracted from library/alloc/src/boxed.rs:376
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        
        let zero = Box::<u32>::try_new_zeroed()?;
        let zero = unsafe { zero.assume_init() };
        
        assert_eq!(*zero, 0);
        Ok::<(), std::alloc::AllocError>(())
    }
    doctest().unwrap();
}
