// Extracted from library/alloc/src/boxed/thin.rs:60
#![allow(unused)]
#![feature(thin_box)]
extern crate alloc;
fn main() {
    use std::boxed::ThinBox;
    
    let five = ThinBox::new(5);
}
