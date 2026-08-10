// Extracted from library/core/src/convert/mod.rs:145
#![allow(unused)]
fn main() {
    let x = Box::new(5i32);
    // Avoid this:
    // let y: &i32 = x.as_ref();
    // Better just write:
    let y: &i32 = &x;
}
