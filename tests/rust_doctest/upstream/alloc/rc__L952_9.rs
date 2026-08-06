// Extracted from library/alloc/src/rc.rs:952
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    
    let x = Rc::new(3);
    assert_eq!(Rc::try_unwrap(x), Ok(3));
    
    let x = Rc::new(4);
    let _y = Rc::clone(&x);
    assert_eq!(*Rc::try_unwrap(x).unwrap_err(), 4);
}
