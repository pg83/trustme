// Extracted from library/alloc/src/vec/mod.rs:3528
#![allow(unused)]
extern crate alloc;
fn main() {
    let x = vec![5, 6, 7];
    let mut y = vec![8, 9, 10];
    let yp: *const i32 = y.as_ptr();

    y.clone_from(&x);

    // The value is the same
    assert_eq!(x, y);

    // And no reallocation occurred
    assert_eq!(yp, y.as_ptr());
}
