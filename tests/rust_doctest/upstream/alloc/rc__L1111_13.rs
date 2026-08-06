// Extracted from library/alloc/src/rc.rs:1111
#![allow(unused)]
#![feature(get_mut_unchecked)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    
    use std::rc::Rc;
    use std::alloc::System;
    
    let mut values = Rc::<[u32], _>::new_uninit_slice_in(3, System);
    
    let values = unsafe {
        // Deferred initialization:
        Rc::get_mut_unchecked(&mut values)[0].as_mut_ptr().write(1);
        Rc::get_mut_unchecked(&mut values)[1].as_mut_ptr().write(2);
        Rc::get_mut_unchecked(&mut values)[2].as_mut_ptr().write(3);
    
        values.assume_init()
    };
    
    assert_eq!(*values, [1, 2, 3])
}
