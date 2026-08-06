// Extracted from library/alloc/src/boxed.rs:721
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        
        let values = Box::<[u32]>::try_new_zeroed_slice(3)?;
        let values = unsafe { values.assume_init() };
        
        assert_eq!(*values, [0, 0, 0]);
        Ok::<(), std::alloc::AllocError>(())
    }
    doctest().unwrap();
}
