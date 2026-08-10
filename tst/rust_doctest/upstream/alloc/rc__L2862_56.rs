// Extracted from library/alloc/src/rc.rs:2862
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    let string: Rc<str> = Rc::from("eggplant");
    let bytes: Rc<[u8]> = Rc::from(string);
    assert_eq!("eggplant".as_bytes(), bytes.as_ref());
}
