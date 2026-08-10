// Extracted from library/core/src/any.rs:440
#![allow(unused)]
#![feature(downcast_unchecked)]
fn main() {

    use std::any::Any;

    let mut x: Box<dyn Any> = Box::new(1_usize);

    unsafe {
        *x.downcast_mut_unchecked::<usize>() += 1;
    }

    assert_eq!(*x.downcast_ref::<usize>().unwrap(), 2);
}
