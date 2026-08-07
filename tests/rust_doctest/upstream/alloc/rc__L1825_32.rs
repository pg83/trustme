// Extracted from library/alloc/src/rc.rs:1825
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;

    let five = Rc::new(5);
    let same_five = Rc::clone(&five);
    let other_five = Rc::new(5);

    assert!(Rc::ptr_eq(&five, &same_five));
    assert!(!Rc::ptr_eq(&five, &other_five));
}
