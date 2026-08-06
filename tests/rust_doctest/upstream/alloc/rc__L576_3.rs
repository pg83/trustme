// Extracted from library/alloc/src/rc.rs:576
#![allow(unused)]
#![feature(allocator_api)]
#![feature(get_mut_unchecked)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        
        use std::rc::Rc;
        
        let mut five = Rc::<u32>::try_new_uninit()?;
        
        // Deferred initialization:
        Rc::get_mut(&mut five).unwrap().write(5);
        
        let five = unsafe { five.assume_init() };
        
        assert_eq!(*five, 5);
        Ok::<(), std::alloc::AllocError>(())
    }
    doctest().unwrap();
}
