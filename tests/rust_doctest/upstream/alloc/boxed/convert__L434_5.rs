// Extracted from library/alloc/src/boxed/convert.rs:434
#![allow(unused)]
#![feature(downcast_unchecked)]
extern crate alloc;
fn main() {
    
    use std::any::Any;
    
    let x: Box<dyn Any + Send> = Box::new(1_usize);
    
    unsafe {
        assert_eq!(*x.downcast_unchecked::<usize>(), 1);
    }
}
