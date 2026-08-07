// Extracted from library/alloc/src/boxed.rs:1751
#![allow(unused)]
extern crate alloc;
fn main() {
    let x = Box::new(5);
    let mut y = Box::new(10);
    let yp: *const i32 = &*y;

    y.clone_from(&x);

    // The value is the same
    assert_eq!(x, y);

    // And no allocation occurred
    assert_eq!(yp, &*y);
}
