// Extracted from library/core/src/primitive_docs.rs:554
#![allow(unused)]
fn main() {
    let my_speed: Box<i32> = Box::new(88);
    let my_speed: *mut i32 = Box::into_raw(my_speed);

    // By taking ownership of the original `Box<T>` though
    // we are obligated to put it together later to be destroyed.
    unsafe {
        drop(Box::from_raw(my_speed));
    }
}
