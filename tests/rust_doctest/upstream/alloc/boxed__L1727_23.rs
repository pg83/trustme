// Extracted from library/alloc/src/boxed.rs:1727
#![allow(unused)]
extern crate alloc;
fn main() {
    let x = Box::new(5);
    let y = x.clone();

    // The value is the same
    assert_eq!(x, y);

    // But they are unique objects
    assert_ne!(&*x as *const i32, &*y as *const i32);
}
