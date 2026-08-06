// Extracted from library/alloc/src/boxed/thin.rs:80
#![allow(unused)]
#![feature(allocator_api)]
#![feature(thin_box)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::boxed::ThinBox;
        
        let five = ThinBox::try_new(5)?;
        Ok::<(), std::alloc::AllocError>(())
    }
    doctest().unwrap();
}
