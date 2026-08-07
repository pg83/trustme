// Extracted from library/alloc/src/rc.rs:2023
#![allow(unused)]
#![feature(downcast_unchecked)]
extern crate alloc;
fn main() {

    use std::any::Any;
    use std::rc::Rc;

    let x: Rc<dyn Any> = Rc::new(1_usize);

    unsafe {
        assert_eq!(*x.downcast_unchecked::<usize>(), 1);
    }
}
