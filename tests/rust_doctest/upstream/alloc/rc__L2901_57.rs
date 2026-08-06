// Extracted from library/alloc/src/rc.rs:2901
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    let evens: Rc<[u8]> = (0..10).filter(|&x| x % 2 == 0).collect();
    assert_eq!(&*evens, &[0, 2, 4, 6, 8]);
}
