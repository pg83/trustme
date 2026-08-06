// Extracted from library/alloc/src/rc.rs:857
#![allow(unused)]
#![feature(allocator_api)]
#![feature(get_mut_unchecked)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        
        use std::rc::Rc;
        use std::alloc::System;
        
        let mut five = Rc::<u32, _>::try_new_uninit_in(System)?;
        
        let five = unsafe {
            // Deferred initialization:
            Rc::get_mut_unchecked(&mut five).as_mut_ptr().write(5);
        
            five.assume_init()
        };
        
        assert_eq!(*five, 5);
        Ok::<(), std::alloc::AllocError>(())
    }
    doctest().unwrap();
}
