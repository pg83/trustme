// Extracted from library/alloc/src/rc.rs:2671
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    let original: [i32; 3] = [1, 2, 3];
    let shared: Rc<[i32]> = Rc::from(original);
    assert_eq!(&[1, 2, 3], &shared[..]);
}
