// Extracted from library/alloc/src/boxed.rs:1783
#![allow(unused)]
extern crate alloc;
fn main() {
    let x = Box::new([5, 6, 7]);
    let mut y = Box::new([8, 9, 10]);
    let yp: *const [i32] = &*y;

    y.clone_from(&x);

    // The value is the same
    assert_eq!(x, y);

    // And no allocation occurred
    assert_eq!(yp, &*y);
}
