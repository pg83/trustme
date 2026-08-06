// Extracted from library/alloc/src/rc.rs:2806
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    let unique: Vec<i32> = vec![1, 2, 3];
    let shared: Rc<[i32]> = Rc::from(unique);
    assert_eq!(&[1, 2, 3], &shared[..]);
}
