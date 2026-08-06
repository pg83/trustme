// Extracted from library/alloc/src/sync.rs:687
#![allow(unused)]
#![feature(get_mut_unchecked)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    
    use std::sync::Arc;
    use std::alloc::System;
    
    let mut five = Arc::<u32, _>::new_uninit_in(System);
    
    let five = unsafe {
        // Deferred initialization:
        Arc::get_mut_unchecked(&mut five).as_mut_ptr().write(5);
    
        five.assume_init()
    };
    
    assert_eq!(*five, 5)
}
