// Extracted from library/alloc/src/rc.rs:1001
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    
    let x = Rc::new(3);
    assert_eq!(Rc::into_inner(x), Some(3));
    
    let x = Rc::new(4);
    let y = Rc::clone(&x);
    
    assert_eq!(Rc::into_inner(y), None);
    assert_eq!(Rc::into_inner(x), Some(4));
}
