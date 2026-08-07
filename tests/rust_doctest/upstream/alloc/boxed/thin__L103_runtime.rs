// Extracted from library/alloc/src/boxed/thin.rs:103
#![allow(unused)]
#![feature(thin_box)]
extern crate alloc;
fn main() {
    use std::boxed::ThinBox;

    let thin_slice = ThinBox::<[i32]>::new_unsize([1, 2, 3, 4]);
}
