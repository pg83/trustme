// Extracted from library/alloc/src/rc.rs:2925
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    let evens: Rc<[u8]> = (0..10).collect(); // Just a single allocation happens here.
    assert_eq!(&*evens, &*(0..10).collect::<Vec<_>>());
}
