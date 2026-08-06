// Extracted from library/core/src/any.rs:547
#![allow(unused)]
#![feature(downcast_unchecked)]
fn main() {
    
    use std::any::Any;
    
    let x: Box<dyn Any> = Box::new(1_usize);
    
    unsafe {
        assert_eq!(*x.downcast_ref_unchecked::<usize>(), 1);
    }
}
