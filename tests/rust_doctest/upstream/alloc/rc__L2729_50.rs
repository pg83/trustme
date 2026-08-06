// Extracted from library/alloc/src/rc.rs:2729
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    let shared: Rc<str> = Rc::from("statue");
    assert_eq!("statue", &shared[..]);
}
