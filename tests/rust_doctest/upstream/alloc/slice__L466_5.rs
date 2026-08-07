// Extracted from library/alloc/src/slice.rs:466
#![allow(unused)]
extern crate alloc;
fn main() {
    let s: Box<[i32]> = Box::new([10, 40, 30]);
    let x = s.into_vec();
    // `s` cannot be used anymore because it has been converted into `x`.

    assert_eq!(x, vec![10, 40, 30]);
}
