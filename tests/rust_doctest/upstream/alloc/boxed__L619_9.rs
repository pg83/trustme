// Extracted from library/alloc/src/boxed.rs:619
#![allow(unused)]
#![feature(box_into_inner)]
extern crate alloc;
fn main() {

    let c = Box::new(5);

    assert_eq!(Box::into_inner(c), 5);
}
