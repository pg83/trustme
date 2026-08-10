// Extracted from library/alloc/src/rc.rs:674
#![allow(unused)]
#![feature(get_mut_unchecked)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {

    use std::rc::Rc;
    use std::alloc::System;

    let mut five = Rc::<u32, _>::new_uninit_in(System);

    let five = unsafe {
        // Deferred initialization:
        Rc::get_mut_unchecked(&mut five).as_mut_ptr().write(5);

        five.assume_init()
    };

    assert_eq!(*five, 5)
}
