// Extracted from library/alloc/src/rc.rs:2650
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    let x = 5;
    let rc = Rc::new(5);

    assert_eq!(Rc::from(x), rc);
}
