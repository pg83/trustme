// Extracted from library/alloc/src/sync.rs:1256
#![allow(unused)]
#![feature(get_mut_unchecked)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    
    use std::sync::Arc;
    use std::alloc::System;
    
    let mut values = Arc::<[u32], _>::new_uninit_slice_in(3, System);
    
    let values = unsafe {
        // Deferred initialization:
        Arc::get_mut_unchecked(&mut values)[0].as_mut_ptr().write(1);
        Arc::get_mut_unchecked(&mut values)[1].as_mut_ptr().write(2);
        Arc::get_mut_unchecked(&mut values)[2].as_mut_ptr().write(3);
    
        values.assume_init()
    };
    
    assert_eq!(*values, [1, 2, 3])
}
