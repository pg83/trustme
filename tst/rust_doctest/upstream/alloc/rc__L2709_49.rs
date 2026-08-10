// Extracted from library/alloc/src/rc.rs:2709
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    let mut original = [1, 2, 3];
    let original: &mut [i32] = &mut original;
    let shared: Rc<[i32]> = Rc::from(original);
    assert_eq!(&[1, 2, 3], &shared[..]);
}
