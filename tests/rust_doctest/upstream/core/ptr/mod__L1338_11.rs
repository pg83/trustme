// Extracted from library/core/src/ptr/mod.rs:1338
#![allow(unused)]
fn main() {
    use std::ptr;

    let mut x = [1, 2, 3, 4];
    let mut y = [7, 8, 9];

    unsafe {
        ptr::swap_nonoverlapping(x.as_mut_ptr(), y.as_mut_ptr(), 2);
    }

    assert_eq!(x, [7, 8, 3, 4]);
    assert_eq!(y, [1, 2, 9]);
}
