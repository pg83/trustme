// Extracted from library/alloc/src/rc.rs:1544
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {

    use std::rc::Rc;
    use std::alloc::System;

    let x: Rc<[u32], _> = Rc::new_in([1, 2, 3], System);
    let x_ptr: *const [u32] = Rc::into_raw_with_allocator(x).0;

    unsafe {
        let x: Rc<[u32; 3], _> = Rc::from_raw_in(x_ptr.cast::<[u32; 3]>(), System);
        assert_eq!(&*x, &[1, 2, 3]);
    }
}
