// Extracted from library/core/src/convert/mod.rs:253
#![allow(unused)]
fn main() {
    let mut x = Box::new(5i32);
    // Avoid this:
    // let y: &mut i32 = x.as_mut();
    // Better just write:
    let y: &mut i32 = &mut x;
}
