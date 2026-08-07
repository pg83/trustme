// Extracted from library/alloc/src/rc.rs:1689
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {

    use std::rc::Rc;
    use std::alloc::System;

    let five = Rc::new_in(5, System);

    unsafe {
        let (ptr, _alloc) = Rc::into_raw_with_allocator(five);
        Rc::increment_strong_count_in(ptr, System);

        let five = Rc::from_raw_in(ptr, System);
        assert_eq!(2, Rc::strong_count(&five));
        Rc::decrement_strong_count_in(ptr, System);
        assert_eq!(1, Rc::strong_count(&five));
    }
}
