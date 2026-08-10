// Extracted from library/alloc/src/rc.rs:2748
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    let mut original = String::from("statue");
    let original: &mut str = &mut original;
    let shared: Rc<str> = Rc::from(original);
    assert_eq!("statue", &shared[..]);
}
