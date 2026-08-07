// Extracted from library/alloc/src/boxed/thin.rs:26
#![allow(unused)]
#![feature(thin_box)]
extern crate alloc;
fn main() {
    use std::boxed::ThinBox;

    let five = ThinBox::new(5);
    let thin_slice = ThinBox::<[i32]>::new_unsize([1, 2, 3, 4]);

    let size_of_ptr = size_of::<*const ()>();
    assert_eq!(size_of_ptr, size_of_val(&five));
    assert_eq!(size_of_ptr, size_of_val(&thin_slice));
}
